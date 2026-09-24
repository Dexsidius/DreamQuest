# =============================================================================
#  blender_creatures.py - the monsters, modelled, animated and rendered into the
#  sheet layout every enemy in the game uses.
#
#  Run headless (tools/make_creatures.ps1 does this):
#      blender --background --python tools/blender_creatures.py -- [creature ...]
#          [--clips idle,walk]
#
#  Each creature is a small tree of joints (empties) with rounded meshes hung on
#  them, built from the same parts, cel shading, majority reduction and
#  selective outline as the player hero (tools/blender_character.py, imported
#  here), so a rat in a cellar and the hero stood over it read as one set. A
#  pose is a dict of joint angles; an animation clip is a function from time to
#  pose. A whole sheet is still one render: every frame of every facing is a
#  copy of the creature, offset along the orthographic camera's axes.
#
#  Output: assets/characters/<id>/<clip>.png -- idle, walk, attack, hurt and
#  death, four rows (down, left, right, up), the shadow composited in. The frame
#  is sized per creature (a rat is small, a wyvern large) at the hero's scale of
#  eighteen game pixels to the unit, with the feet the same fraction of the way
#  down the frame as the hero's, so data/sprites.json's anchor lands them on
#  their position.
# =============================================================================

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_character as bc  # noqa: E402

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Euler, Vector  # noqa: E402

UNITS_PER_PX = 3.5 / 64.0     # the hero's scale
OUT_ROOT = os.path.join(bc.ROOT, "assets", "characters")
RENDER_DIR = os.path.join(bc.ROOT, "assets", "_render", "creatures")

# --- colours ---------------------------------------------------------------------
bc.PALETTE.update({
    # rat
    "rat_fur": (0.55, 0.48, 0.44), "rat_fur_dk": (0.40, 0.34, 0.32), "rat_pink": (0.91, 0.64, 0.64),
    "rat_eye_glow": (0.95, 0.25, 0.20),
    # spider
    "spider": (0.30, 0.24, 0.22), "spider_dk": (0.18, 0.14, 0.14), "spider_mark": (0.84, 0.72, 0.52),
    "spider_eye_glow": (1.00, 0.20, 0.16), "fang": (0.92, 0.88, 0.80),
    # lizardman
    "liz": (0.30, 0.52, 0.31), "liz_dk": (0.18, 0.33, 0.21), "liz_belly": (0.78, 0.78, 0.55),
    "liz_eye_glow": (1.00, 0.72, 0.16), "loincloth": (0.50, 0.34, 0.22), "spear": (0.46, 0.34, 0.22),
    "bone": (0.90, 0.86, 0.74), "crest": (0.13, 0.20, 0.16),
    # The heavier build: a lighter back, a plated belly, the yellow throat
    # frill, the pelt over the shoulders and the rag on the spear.
    "liz_lt": (0.41, 0.63, 0.36), "liz_plate": (0.55, 0.56, 0.34), "liz_frill": (0.92, 0.84, 0.28),
    "liz_claw": (0.28, 0.26, 0.20), "pelt": (0.80, 0.78, 0.70), "pelt_dk": (0.52, 0.49, 0.43),
    "rag_red": (0.72, 0.16, 0.14),
    # ice troll
    "troll": (0.64, 0.76, 0.86), "troll_dk": (0.44, 0.56, 0.68), "troll_fur": (0.92, 0.94, 0.96),
    "troll_eye_glow": (0.40, 0.95, 1.00), "ice": (0.66, 0.90, 0.98), "ice_dk": (0.40, 0.70, 0.86),
    "tusk": (0.95, 0.92, 0.82), "hide_grey": (0.52, 0.50, 0.48),
    # wyvern
    "wyv": (0.44, 0.56, 0.72), "wyv_dk": (0.28, 0.36, 0.50), "wyv_belly": (0.84, 0.90, 0.94),
    "wyv_wing": (0.52, 0.80, 0.90), "horn": (0.92, 0.90, 0.84), "wyv_eye_glow": (0.60, 1.00, 1.00),
    # demon and imp
    "demon": (0.64, 0.17, 0.14), "demon_dk": (0.38, 0.08, 0.10), "demon_horn": (0.20, 0.16, 0.15),
    "demon_eye_glow": (1.00, 0.84, 0.30), "ember_glow": (1.00, 0.52, 0.16), "wing_dk": (0.30, 0.10, 0.12),
    "imp": (0.82, 0.34, 0.20), "imp_dk": (0.56, 0.18, 0.14), "iron_dk": (0.30, 0.30, 0.34),
    # the dead of Hollowrest: rot, old bone, and whatever a wraith is
    "rot": (0.55, 0.60, 0.44), "rot_dk": (0.38, 0.43, 0.31), "rot_wound": (0.47, 0.25, 0.24),
    "rot_cloth": (0.38, 0.35, 0.30), "rot_cloth_dk": (0.27, 0.25, 0.22),
    "rot_eye_glow": (0.78, 0.94, 0.42),
    "bone_w": (0.86, 0.84, 0.75), "bone_g": (0.70, 0.68, 0.58), "bone_dk": (0.52, 0.50, 0.42),
    "rust": (0.46, 0.31, 0.21), "rust_lt": (0.60, 0.44, 0.30),
    "shroud": (0.31, 0.31, 0.40), "shroud_dk": (0.20, 0.20, 0.28), "shroud_lt": (0.44, 0.45, 0.56),
    "wisp_glow": (0.58, 0.88, 0.94), "grave_gold": (0.74, 0.64, 0.32),
    # what lives down the well: slime, bats, hounds, and the two that stopped
    # being alive some time ago
    "slime": (0.44, 0.72, 0.46), "slime_dk": (0.28, 0.52, 0.34), "slime_lt": (0.68, 0.90, 0.62),
    "slime_core": (0.86, 0.96, 0.66),
    "slime_eye": (0.96, 0.99, 0.94), "slime_pupil": (0.06, 0.10, 0.08), "bat_fur": (0.34, 0.28, 0.30), "bat_fur_dk": (0.22, 0.18, 0.20),
    "bat_wing": (0.42, 0.33, 0.36), "bat_eye_glow": (1.00, 0.62, 0.30),
    # Ash rather than soot: a black dog on black stone in a map with no light
    # of its own is a silhouette the player never sees coming.
    "hound": (0.46, 0.42, 0.42), "hound_dk": (0.30, 0.27, 0.29), "hound_eye_glow": (1.00, 0.46, 0.20),
    "ankou_robe": (0.26, 0.26, 0.31), "ankou_robe_lt": (0.38, 0.38, 0.45),
    "ankou_bone": (0.84, 0.82, 0.74), "ankou_eye_glow": (0.42, 1.00, 0.72), "scythe_steel": (0.62, 0.66, 0.70),
    "banshee": (0.62, 0.78, 0.88), "banshee_dk": (0.40, 0.56, 0.70), "banshee_hair": (0.84, 0.90, 0.96),
    "banshee_glow": (0.72, 0.94, 1.00),
    # The orcs: three sizes of the same build, in their own art rather than a
    # pack's, so the character sheets can travel with the repository.
    "orc_skin": (0.45, 0.56, 0.37), "orc_skin_dk": (0.33, 0.43, 0.27),
    "orc_hide": (0.46, 0.33, 0.22), "orc_hide_dk": (0.31, 0.22, 0.15),
    "orc_iron": (0.56, 0.58, 0.62), "orc_iron_dk": (0.38, 0.40, 0.44),
    "orc_steel": (0.74, 0.77, 0.80), "orc_tusk": (0.91, 0.89, 0.78),
    "orc_eye": (0.97, 0.74, 0.22), "orc_war_red": (0.58, 0.19, 0.17),
    "orc_gold": (0.80, 0.66, 0.28),
    # The woodland animals. They were a CraftPix pack; these are the game's own,
    # so assets/characters can be redistributed with it.
    "boar_hide": (0.36, 0.28, 0.23), "boar_hide_dk": (0.25, 0.19, 0.16),
    "boar_bristle": (0.17, 0.13, 0.12), "boar_snout": (0.45, 0.34, 0.31),
    "boar_tusk": (0.92, 0.90, 0.80),
    "deer_hide": (0.66, 0.38, 0.21), "deer_hide_dk": (0.46, 0.25, 0.14),
    "deer_belly": (0.93, 0.85, 0.70), "deer_antler": (0.86, 0.79, 0.62),
    "deer_hoof": (0.26, 0.20, 0.16),
    "fox_fur": (0.80, 0.41, 0.15), "fox_fur_dk": (0.61, 0.28, 0.10),
    "fox_belly": (0.95, 0.91, 0.85), "fox_sock": (0.21, 0.17, 0.16),
    "hare_fur": (0.68, 0.54, 0.37), "hare_fur_dk": (0.51, 0.39, 0.27),
    "hare_belly": (0.92, 0.88, 0.80), "hare_ear_in": (0.82, 0.63, 0.57),
    "beast_eye": (0.10, 0.08, 0.07),
    # The Westwold's wolves and the Brackenwood's bears.
    "wolf_fur": (0.56, 0.55, 0.54), "wolf_fur_dk": (0.36, 0.35, 0.36),
    "wolf_belly": (0.86, 0.84, 0.80), "wolf_nose": (0.14, 0.12, 0.12),
    "bear_fur": (0.40, 0.27, 0.17), "bear_fur_dk": (0.27, 0.18, 0.12),
    "bear_muzzle": (0.66, 0.52, 0.37), "bear_claw": (0.88, 0.85, 0.76),
    # Elder Vask, and the chair he has not got out of in some years
    "vask_robe": (0.42, 0.40, 0.36), "vask_robe_dk": (0.30, 0.29, 0.26),
    "vask_shawl": (0.45, 0.33, 0.28), "vask_blanket": (0.38, 0.30, 0.34),
    "vask_skin": (0.86, 0.71, 0.58), "vask_skin_dk": (0.70, 0.56, 0.45),
    "vask_hair": (0.90, 0.89, 0.86), "vask_hair_dk": (0.72, 0.71, 0.68),
    "chair_wood": (0.44, 0.30, 0.19), "chair_wood_lt": (0.56, 0.40, 0.26),
    "chair_wood_dk": (0.32, 0.21, 0.14), "vask_stick": (0.38, 0.26, 0.16),
    # the dragon of the Ice Spire: hoarfrost over deep glacier blue
    "drake": (0.63, 0.74, 0.86), "drake_dk": (0.36, 0.49, 0.67), "drake_belly": (0.90, 0.94, 0.97),
    "drake_wing": (0.58, 0.80, 0.94), "drake_horn": (0.82, 0.88, 0.94),
    "drake_eye_glow": (0.55, 0.95, 1.00), "rime_glow": (0.78, 0.96, 1.00),
})

_toon = bc.material


def material(colour):
    """The hero's cel material; anything named *_glow is flat light instead,
    so eyes and embers read as lit from inside."""
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
    links.new(emit.outputs["Emission"], out.inputs["Surface"])
    bc._materials[colour] = mat
    return mat


bc.material = material
E = bc.mesh_ellipsoid
C = bc.mesh_capsule
part, spike = bc.part, bc.spike


# --- rigs --------------------------------------------------------------------------
class Rig:
    """Joints by name, each with a rest rotation in degrees. `pose` is the
    empty every visible part hangs from, below the root; the root only turns
    the creature to its facing and places it in the sheet."""

    def __init__(self):
        self.root = bc.empty("root", (0, 0, 0))
        self.pose = bc.empty("pose", (0, 0, 0), self.root)
        self.j = {"pose": self.pose}
        self.rest = {}
        self.parts = []
        self.fall = Vector((0, 0, 0))     # world-space shift, set by apply()

    def joint(self, name, loc, parent="pose", rest=(0, 0, 0)):
        self.j[name] = bc.empty(name, loc, self.j[parent])
        self.rest[name] = rest
        return self.j[name]

    def add(self, name, mesh, colour, parent, loc=(0, 0, 0), rot=(0, 0, 0)):
        ob = part(name, mesh, colour, self.j[parent], loc=loc, rot=rot)
        self.parts.append(ob)
        return ob

    def limb(self, name, a, b, r, colour, parent, r_tip=None):
        ob = spike(name, a, b, r, colour, self.j[parent], r_tip=r if r_tip is None else r_tip)
        self.parts.append(ob)
        return ob

    def standing_height(self):
        """How tall the rig is before it is posed, in world units.

        Measured off the rig rather than written down per creature: a topple
        has to be pulled back by half a body length to stay in the middle of
        its cell, and the one number that says how long a body is belongs to
        the body. Called before any joint is turned, so it is always the rest
        height."""
        bpy.context.view_layer.update()
        top = 0.0
        for ob in self.parts:
            for corner in ob.bound_box:
                top = max(top, (ob.matrix_world @ Vector(corner)).z)
        return top

    def apply(self, values, turn=0.0):
        # The world-space fall, worked out before anything is posed so the
        # height is the rig's own.
        wroll = values.get("_wroll", 0.0)
        fall = Vector((values.get("_wx", 0.0), values.get("_wy", 0.0),
                       values.get("_wz", 0.0)))
        if wroll:
            h = self.standing_height()
            # It goes over about its feet, so it ends up a body length to one
            # side. Pull it back by half of that, and do not let it go so far
            # over that its head lands in the neighbouring frame: a small
            # creature lies flat, a big one finishes propped on its shoulder.
            fit = min(1.0, (bc.FRAME_SPAN * 0.74) / max(1e-6, h))
            wroll = math.degrees(math.asin(
                min(1.0, fit * abs(math.sin(math.radians(wroll))))))
            wroll = math.copysign(wroll, values["_wroll"])
            fall.x -= 0.5 * h * math.sin(math.radians(wroll))
        self.fall = fall

        # "_straighten" fades a joint's rest angle out rather than adding to
        # it: {"elbow_r": 0.8} means four-fifths of the way to straight. A limb
        # holding something long needs this -- what matters is its total angle,
        # and every creature's rest angle is different, so "swing it back by
        # twenty degrees" lands somewhere different on each of them.
        straighten = values.get("_straighten", {})
        for name, e in self.j.items():
            if name == "pose":
                continue
            base = self.rest.get(name, (0, 0, 0))
            f = 1.0 - straighten.get(name, 0.0)
            add = values.get(name, (0, 0, 0))
            e.rotation_euler = Euler(tuple(math.radians(base[i] * f + add[i]) for i in range(3)), "XYZ")
        self.pose.location = (values.get("_x", 0.0), values.get("_y", 0.0), values.get("_z", 0.0))
        roll = values.get("_roll", 0.0)
        if values.get("_roll_to_camera") and abs(math.sin(math.radians(turn))) > 0.5:
            # A four-legged animal goes down onto its side, which is a roll
            # about its own spine -- the right axis for it, unlike a humanoid's
            # fall. But which side it lands on decides where its legs point.
            # Side on, legs rolled away from the camera stand straight up the
            # screen and the carcass reads as a deer balanced on its back; rolled
            # towards the camera they lie down the screen under it. Facing down
            # or up either side is fine, so only the two side rows are turned.
            roll = -math.copysign(abs(roll), math.sin(math.radians(turn)))
        self.pose.rotation_euler = Euler((math.radians(values.get("_pitch", 0.0)),
                                          math.radians(roll), 0.0), "XYZ")
        # _pitch and _roll above are in the creature's own frame, which is what
        # a lean or a flinch wants. A fall is not: a body that tips over its own
        # backwards axis topples away from the camera in the row where it faced
        # the camera, and towards it in the row where it faced away, and both of
        # those foreshorten to a standing blob. _wpitch and _wroll are applied
        # outside the facing turn -- Euler order ZYX puts the turn innermost --
        # so a fall goes the same way across the screen whichever way the
        # creature was looking. _wx / _wy / _wz shift it along the world axes
        # for the same reason: a body lying across the frame has to be pulled
        # back towards the middle of its cell or its head hangs off the edge.
        # The order lives on the object, not on the Euler handed to it: an
        # object keeps its own rotation_mode and reads only the three numbers,
        # so assigning an Euler built as "ZYX" to an "XYZ" object silently
        # applies them in the wrong order -- which put the fall back inside the
        # facing and moved the problem to the other two rows.
        self.root.rotation_mode = "ZYX"
        self.root.rotation_euler = Euler((math.radians(values.get("_wpitch", 0.0)),
                                          math.radians(wroll), math.radians(turn)), "ZYX")


def sn(t, phase=0.0):
    return math.sin((t + phase) * math.tau)


def ease(k):
    k = max(0.0, min(1.0, k))
    return k * k * (3 - 2 * k)


def phases(t, *marks):
    """Which stretch of a one-shot t falls in, and how far through it: marks are
    the fractions where stretches end."""
    start = 0.0
    for i, m in enumerate(marks):
        if t <= m:
            return i, ease((t - start) / max(1e-6, m - start))
        start = m
    return len(marks), 1.0


def mix(a, b, k):
    keys = set(a) | set(b)
    out = {}
    for key in keys:
        va, vb = a.get(key, 0.0), b.get(key, 0.0)
        if isinstance(va, tuple) or isinstance(vb, tuple):
            va = va if isinstance(va, tuple) else (0.0, 0.0, 0.0)
            vb = vb if isinstance(vb, tuple) else (0.0, 0.0, 0.0)
            out[key] = tuple(va[i] + (vb[i] - va[i]) * k for i in range(3))
        else:
            out[key] = va + (vb - va) * k
    return out


def X(d):
    """Pitch about the joint's X. Negative swings a hanging limb forward, toward
    the creature's face (-Y)."""
    return (d, 0.0, 0.0)


# A leg hangs along -Z from its joint. fwd(deg) swings it toward the face.
def fwd(d):
    return (-d, 0.0, 0.0)


# =================================================================================
#  Rat
# =================================================================================
def build_rat():
    r = Rig()
    r.joint("body", (0, 0.05, 0.30))
    r.add("torso", E(0.23, 0.36, 0.20), "rat_fur", "body")
    r.add("rump", E(0.25, 0.22, 0.22), "rat_fur", "body", loc=(0, 0.24, 0.02))
    r.add("back", E(0.16, 0.30, 0.08), "rat_fur_dk", "body", loc=(0, 0.10, 0.15))
    r.joint("head", (0, -0.34, 0.06), "body")
    r.add("skull", E(0.16, 0.20, 0.15), "rat_fur", "head")
    r.add("snout", E(0.08, 0.14, 0.08), "rat_fur", "head", loc=(0, -0.19, -0.04))
    r.add("nose", E(0.035, 0.035, 0.03), "rat_pink", "head", loc=(0, -0.32, -0.03))
    for s in (-1, 1):
        r.add("ear", E(0.09, 0.03, 0.10), "rat_pink", "head", loc=(s * 0.12, 0.02, 0.14), rot=(0, s * 0.4, 0))
        r.add("eye", E(0.03, 0.03, 0.035), "rat_eye_glow", "head", loc=(s * 0.09, -0.13, 0.05))
    for name, x, y in (("fl", -0.13, -0.22), ("fr", 0.13, -0.22), ("bl", -0.15, 0.26), ("br", 0.15, 0.26)):
        r.joint("leg_" + name, (x, y, -0.06), "body")
        r.limb("leg", (0, 0, 0), (0, 0, -0.20), 0.05, "rat_fur_dk", "leg_" + name, r_tip=0.035)
        r.add("paw", E(0.05, 0.07, 0.03), "rat_pink", "leg_" + name, loc=(0, -0.03, -0.22))
    r.joint("tail1", (0, 0.44, 0.0), "body", rest=(-20, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0.30, 0), 0.04, "rat_pink", "tail1", r_tip=0.03)
    r.joint("tail2", (0, 0.30, 0), "tail1", rest=(15, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0.32, 0), 0.03, "rat_pink", "tail2", r_tip=0.012)
    return r


def rat_idle(t):
    return {"_z": 0.01 * sn(t), "head": (4 * sn(t * 2), 0, 6 * sn(t)), "tail1": (0, 0, 14 * sn(t)),
            "tail2": (0, 0, 18 * sn(t, 0.25))}


def rat_walk(t):
    s = sn(t)
    return {"_z": 0.03 * abs(sn(t)), "leg_fl": fwd(38 * s), "leg_br": fwd(38 * s),
            "leg_fr": fwd(-38 * s), "leg_bl": fwd(-38 * s), "tail1": (0, 0, 20 * s), "tail2": (0, 0, 24 * sn(t, 0.2)),
            "head": (0, 0, 5 * s), "body": (3 * sn(t * 2), 0, 0)}


def rat_attack(t):
    i, k = phases(t, 0.35, 0.55, 1.0)
    crouch = {"_z": -0.05, "_y": 0.10, "head": X(-12), "leg_fl": fwd(-20), "leg_fr": fwd(-20),
              "body": X(8), "tail1": (0, 0, 10)}
    bite = {"_z": 0.08, "_y": -0.30, "head": X(18), "leg_fl": fwd(50), "leg_fr": fwd(50),
            "leg_bl": fwd(-30), "leg_br": fwd(-30), "body": X(-10), "tail1": (10, 0, -10)}
    return [mix({}, crouch, k), mix(crouch, bite, k), mix(bite, {}, k), {}][i]


def rat_hurt(t):
    k = math.sin(t * math.pi)
    return {"_y": 0.15 * k, "_z": 0.04 * k, "head": X(-20 * k), "body": X(-10 * k), "tail1": (20 * k, 0, 0)}


def rat_death(t):
    k = ease(t * 1.3)
    return {"_roll": 90 * k, "_z": -0.10 * k, "leg_fl": fwd(30 * k), "leg_fr": fwd(-20 * k),
            "leg_bl": fwd(-35 * k), "leg_br": fwd(25 * k), "head": X(10 * k), "tail1": (0, 0, 30 * k)}


# =================================================================================
#  Spider
# =================================================================================
def build_spider():
    r = Rig()
    r.joint("body", (0, 0, 0.30))
    r.add("thorax", E(0.20, 0.22, 0.13), "spider", "body", loc=(0, -0.10, 0))
    r.add("abdomen", E(0.30, 0.36, 0.27), "spider_dk", "body", loc=(0, 0.34, 0.10))
    r.add("mark", E(0.10, 0.20, 0.05), "spider_mark", "body", loc=(0, 0.30, 0.34))
    r.add("mark2", E(0.18, 0.05, 0.04), "spider_mark", "body", loc=(0, 0.42, 0.33))
    for s in (-1, 1):
        for k in range(2):
            r.add("eye", E(0.035, 0.03, 0.03), "spider_eye_glow", "body", loc=(s * (0.05 + k * 0.06), -0.29, 0.06 + k * 0.03))
        r.limb("fang", (s * 0.05, -0.30, -0.02), (s * 0.03, -0.36, -0.14), 0.03, "fang", "body", r_tip=0.01)
    # Eight legs, knees high: out and up to the knee, then down to the foot.
    spread = (-55, -20, 20, 55)
    for side in (-1, 1):
        for i, ang in enumerate(spread):
            name = "leg_%s%d" % ("l" if side < 0 else "r", i)
            a = math.radians(ang)
            r.joint(name, (side * 0.14, -0.10 + math.sin(a) * 0.12, 0.0), "body", rest=(0, 0, side * ang))
            knee = (side * 0.34, 0, 0.24)
            foot = (side * 0.62, 0, -0.30)
            r.limb("thigh", (0, 0, 0), knee, 0.045, "spider", name, r_tip=0.035)
            r.limb("shin", knee, foot, 0.035, "spider_dk", name, r_tip=0.015)
    return r


def spider_legs(t, amp, lift):
    out = {}
    for side in ("l", "r"):
        for i in range(4):
            group = (i + (side == "r")) % 2
            s = sn(t, 0.5 * group)
            out["leg_%s%d" % (side, i)] = (-lift * max(0.0, s), 0.0, amp * s)
    return out


def spider_idle(t):
    v = spider_legs(t, 3, 4)
    v["_z"] = 0.015 * sn(t)
    return v


def spider_walk(t):
    v = spider_legs(t, 16, 14)
    v["_z"] = 0.02 * abs(sn(t * 2))
    v["body"] = (0, 0, 4 * sn(t))
    return v


def spider_attack(t):
    i, k = phases(t, 0.4, 0.6, 1.0)
    rear = {"body": X(-22), "_z": 0.10, "_y": 0.06}
    for s in ("l", "r"):
        rear["leg_%s0" % s] = (-45, 0, 0)
        rear["leg_%s1" % s] = (-25, 0, 0)
    strike = {"body": X(14), "_z": 0.0, "_y": -0.28}
    for s in ("l", "r"):
        strike["leg_%s0" % s] = (20, 0, 0)
    return [mix({}, rear, k), mix(rear, strike, k), mix(strike, {}, k), {}][i]


def spider_hurt(t):
    k = math.sin(t * math.pi)
    v = {"_y": 0.12 * k, "body": X(-12 * k)}
    for s in ("l", "r"):
        for i in range(4):
            v["leg_%s%d" % (s, i)] = (-20 * k, 0, 0)
    return v


def spider_death(t):
    k = ease(t * 1.2)
    v = {"_roll": 180 * k, "_z": 0.30 * k * (1 - k) + 0.28 * k}
    for s in ("l", "r"):
        for i in range(4):
            v["leg_%s%d" % (s, i)] = (-70 * k, 0, 0)
    return v


# =================================================================================
#  Humanoid parts: lizardman, ice troll, demon
# =================================================================================
def humanoid_legs(r, hip_z, hip_x, thigh, shin, r_leg, colour, dark, digitigrade=False, foot_col=None):
    for s, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (s * hip_x, 0, hip_z), "pelvis")
        r.limb("thigh", (0, 0, 0), (0, 0, -thigh), r_leg, colour, "hip_" + side, r_tip=r_leg * 0.8)
        r.joint("knee_" + side, (0, 0, -thigh), "hip_" + side, rest=(-30 if digitigrade else 0, 0, 0))
        r.limb("shin", (0, 0, 0), (0, 0, -shin), r_leg * 0.8, dark, "knee_" + side, r_tip=r_leg * 0.6)
        r.add("foot", E(r_leg * 0.9, r_leg * 1.6, r_leg * 0.5), foot_col or dark, "knee_" + side,
              loc=(0, -r_leg * 0.9, -shin))


def humanoid_arms(r, sh_z, sh_x, upper, fore, r_arm, colour, dark, flare=12):
    for s, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (s * sh_x, 0, sh_z), "chest", rest=(0, s * -flare, 0))
        r.limb("upper", (0, 0, 0), (0, 0, -upper), r_arm, colour, "shoulder_" + side, r_tip=r_arm * 0.85)
        r.joint("elbow_" + side, (0, 0, -upper), "shoulder_" + side, rest=(-15, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -fore), r_arm * 0.85, dark, "elbow_" + side, r_tip=r_arm * 0.7)
        r.joint("hand_" + side, (0, 0, -fore), "elbow_" + side)
        r.add("hand", E(r_arm * 0.9, r_arm * 0.9, r_arm), dark, "hand_" + side)


def gait(t, legs=34, knees=30, arms=26, bob=0.03, lean=6):
    s = sn(t)
    return {"hip_l": fwd(legs * s), "hip_r": fwd(-legs * s),
            "knee_l": X(knees * max(0.0, -s)), "knee_r": X(knees * max(0.0, s)),
            "shoulder_l": fwd(-arms * s), "shoulder_r": fwd(arms * s),
            "_z": bob * abs(sn(t)), "chest": X(lean)}


def running(walk_fn, stretch=1.5, lean=5, bob=1.7):
    """A run built out of the creature's own walk: the same cycle, opened up.

    Anything that runs already has a walk that says how it moves -- which legs
    swing against which, how the body rides over them -- so the run borrows it
    instead of inventing a second gait that would not match the first. Longer
    strides, more lift, and a lean into it.
    """
    def pose(t):
        v = dict(walk_fn(t))
        for name, value in list(v.items()):
            if name.startswith("_"):
                continue
            v[name] = tuple(a * stretch for a in value)
        v["_z"] = v.get("_z", 0.0) * bob
        v["chest"] = tuple(a + b for a, b in zip(v.get("chest", (0, 0, 0)), (lean, 0, 0)))
        return v
    return pose


def topple(t, dir=1, lean=88, armed=False):
    """Go down sideways and lie there, across the frame.

    This used to tip the body over its own backwards axis, which looks right
    from the side and nowhere else: in the row where the creature faced the
    camera it fell away from it, in the row where it faced away it fell towards
    it, and in both the body foreshortened into a standing lump that never
    seemed to drop at all. Toppling about the world's own sideways axis instead
    gives every row the one direction a camera looking down at forty-six
    degrees can actually read, so all four rows end as a body lying flat.

    Keeping the body inside its cell is Rig.apply's job: it knows how tall the
    rig is, so it pulls the fall back by half a body length and eases off the
    angle for a creature too long to lie flat in one frame.

    `armed` leaves the right arm hanging. Flinging it forward is right for empty
    hands, but a spear or a club held that way ends up pointing straight at the
    camera, where the fall cannot lay it down: it stays standing in the frame
    like a planted pole while the body under it goes flat. An arm left along the
    body puts the weapon on the ground beside it.
    """
    k = ease(t * 1.15)
    s = ease(min(1.0, t * 2.0))               # the legs give first
    # A body lying on the ground still has thickness, and the frame's anchor is
    # the feet: without the lift the near side of it hangs a few pixels below
    # the cell and smudges the frame underneath.
    return {"_wroll": dir * lean * k, "_z": -0.04 * k, "_wz": 0.22 * k,
            "hip_l": fwd(24 * s), "hip_r": fwd(12 * s),
            "knee_l": X(30 * s), "knee_r": X(18 * s),
            "shoulder_l": fwd(64 * k),
            "shoulder_r": (0, 0, 0) if armed else fwd(52 * k),
            "elbow_l": X(-28 * k), "elbow_r": (0, 0, 0) if armed else X(-16 * k),
            "chest": X(-10 * k), "neck": X(-22 * k),
            # The weapon arm is laid straight down the body, rest angles and
            # all, so what it holds lies in the same plane the body falls in.
            # Any angle left in it points the weapon towards or away from the
            # camera, where a sideways fall cannot lay it down -- and that is
            # the one direction the frame has no room in.
            "_straighten": {"shoulder_r": k, "elbow_r": k, "hand_r": k} if armed else {}}


# --- lizardman ------------------------------------------------------------------------
def build_lizardman():
    """A heavy reptilian warrior rather than a lizard standing up: shoulders
    wider than its hips, a slab of a chest with belly plates down it, a jawed
    head carried forward on a thick neck under a crest of backswept spines,
    the yellow throat frill they display with, a pelt over one shoulder and a
    bone-headed spear. The first one was built at a townsfolk's proportions and
    read as something a hero could step over."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.74))
    r.add("hips", E(0.25, 0.20, 0.16), "liz", "pelvis")
    r.add("rump", E(0.23, 0.16, 0.14), "liz_dk", "pelvis", loc=(0, 0.10, -0.02))

    # --- legs: heavy thighs, a digitigrade shin, three claws on a broad foot --
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.16, 0, -0.04), "pelvis")
        r.limb("thigh", (0, 0, 0), (0, -0.02, -0.34), 0.115, "liz", "hip_" + side, r_tip=0.085)
        r.joint("knee_" + side, (0, -0.02, -0.34), "hip_" + side, rest=(-30, 0, 0))
        r.limb("shin", (0, 0, 0), (0, 0, -0.36), 0.075, "liz_dk", "knee_" + side, r_tip=0.055)
        r.add("foot", E(0.085, 0.14, 0.05), "liz_dk", "knee_" + side, loc=(0, -0.07, -0.33))
        for k in (-1, 0, 1):
            r.limb("claw", (k * 0.05, -0.13, -0.34), (k * 0.07, -0.20, -0.35), 0.022, "liz_claw",
                   "knee_" + side, r_tip=0.005)

    # --- torso: broad, leaning forward, plated down the front -----------------
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(6, 0, 0))
    r.add("torso", E(0.29, 0.20, 0.30), "liz", "chest", loc=(0, 0, 0.24))
    r.add("back", E(0.25, 0.13, 0.20), "liz", "chest", loc=(0, 0.07, 0.28))
    for k in range(3):
        r.add("scute", E(0.10 - k * 0.015, 0.05, 0.035), "liz_lt", "chest", loc=(0, 0.13, 0.42 - k * 0.12))
    for k in range(4):
        r.add("plate", E(0.15 - k * 0.012, 0.05, 0.055), "liz_belly", "chest", loc=(0, -0.14, 0.09 + k * 0.10))
    r.add("gut", E(0.16, 0.11, 0.10), "liz_belly", "chest", loc=(0, -0.09, 0.02))
    # Dorsal spines, neck to tail.
    for k in range(5):
        r.limb("spine", (0, 0.10, 0.44 - k * 0.09), (0, 0.16, 0.54 - k * 0.09), 0.028, "crest", "chest",
               r_tip=0.005)
    # A pelt over the left shoulder, tied across the chest.
    for k in range(3):
        r.add("pelt", E(0.085 - k * 0.012, 0.075, 0.055), "pelt" if k % 2 == 0 else "pelt_dk", "chest",
              loc=(-0.17 - k * 0.02, -0.06 + k * 0.05, 0.46 - k * 0.06))
    r.add("pelt_tail", E(0.06, 0.055, 0.11), "pelt_dk", "chest", loc=(-0.20, -0.08, 0.30))
    r.add("strap", E(0.17, 0.12, 0.035), "loincloth", "chest", loc=(0.02, -0.12, 0.26), rot=(0, 0, 0.5))

    # --- arms: thick, with three-clawed hands ---------------------------------
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.29, 0, 0.40), "chest", rest=(0, sx * -14, 0))
        r.add("deltoid", E(0.105, 0.105, 0.10), "liz_lt", "shoulder_" + side)
        r.limb("upper", (0, 0, 0), (0, 0, -0.26), 0.085, "liz", "shoulder_" + side, r_tip=0.07)
        r.joint("elbow_" + side, (0, 0, -0.26), "shoulder_" + side, rest=(-18, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.25), 0.07, "liz_dk", "elbow_" + side, r_tip=0.055)
        r.joint("hand_" + side, (0, 0, -0.25), "elbow_" + side)
        r.add("hand", E(0.075, 0.07, 0.08), "liz_dk", "hand_" + side)
        for k in (-1, 0, 1):
            r.limb("finger", (k * 0.04, -0.03, -0.06), (k * 0.055, -0.09, -0.09), 0.018, "liz_claw",
                   "hand_" + side, r_tip=0.004)

    # --- belt and loincloth ----------------------------------------------------
    r.add("belt", E(0.26, 0.21, 0.035), "loincloth", "pelvis", loc=(0, 0, 0.06))
    r.add("cloth_front", E(0.13, 0.05, 0.19), "loincloth", "pelvis", loc=(0, -0.16, -0.10))
    r.add("cloth_back", E(0.12, 0.05, 0.15), "liz_plate", "pelvis", loc=(0, 0.15, -0.10))
    r.add("pouch", E(0.06, 0.05, 0.07), "pelt_dk", "pelvis", loc=(0.19, -0.06, 0.0))

    # --- neck and head: carried forward, jaw first ----------------------------
    r.joint("neck", (0, -0.02, 0.58), "chest", rest=(2, 0, 0))
    r.add("neckp", C(0.11, 0.125, 0.20), "liz", "neck", loc=(0, 0, 0.10))
    r.joint("head", (0, -0.01, 0.21), "neck", rest=(-2, 0, 0))
    r.add("skull", E(0.155, 0.17, 0.145), "liz", "head", loc=(0, 0, 0.05))
    r.add("brow", E(0.155, 0.09, 0.05), "liz_lt", "head", loc=(0, -0.10, 0.12))
    r.add("snout", E(0.115, 0.17, 0.095), "liz", "head", loc=(0, -0.20, 0.02))
    r.add("jaw", E(0.10, 0.16, 0.055), "liz_belly", "head", loc=(0, -0.19, -0.07))
    for sx in (-1, 1):
        r.add("eye", E(0.042, 0.035, 0.036), "liz_eye_glow", "head", loc=(sx * 0.105, -0.11, 0.10))
        r.add("nostril", E(0.02, 0.02, 0.018), "liz_dk", "head", loc=(sx * 0.04, -0.37, 0.03))
        # Teeth in the upper jaw, and a horn behind each eye.
        for k in range(3):
            r.limb("tooth", (sx * (0.06 + k * 0.012), -0.16 - k * 0.07, -0.05),
                   (sx * (0.06 + k * 0.012), -0.16 - k * 0.07, -0.10), 0.016, "bone", "head", r_tip=0.003)
        r.limb("horn", (sx * 0.12, 0.02, 0.10), (sx * 0.20, 0.16, 0.18), 0.035, "crest", "head", r_tip=0.006)
    # The crest: five backswept spines, longest in the middle.
    for k in range(5):
        off = abs(k - 2)
        r.limb("crest", (0, 0.02 + k * 0.045 - 0.09, 0.17), (0, 0.14 + k * 0.05 - 0.09, 0.34 - off * 0.05),
               0.032 - off * 0.005, "crest", "head", r_tip=0.005)
    # The throat frill they display with: yellow, and the loudest thing on them.
    r.add("frill", E(0.115, 0.09, 0.13), "liz_frill", "head", loc=(0, -0.17, -0.16))
    for sx in (-1, 1):
        r.add("frill_lobe", E(0.05, 0.05, 0.08), "liz_frill", "head", loc=(sx * 0.08, -0.12, -0.20))

    # --- tail: three segments, thick at the root, plated on top ---------------
    r.joint("tail1", (0, 0.18, -0.06), "pelvis", rest=(62, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.30), 0.135, "liz", "tail1", r_tip=0.10)
    r.joint("tail2", (0, 0, -0.30), "tail1", rest=(16, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.27), 0.10, "liz", "tail2", r_tip=0.065)
    r.joint("tail3", (0, 0, -0.27), "tail2", rest=(14, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.24), 0.065, "liz_dk", "tail3", r_tip=0.018)
    for k in range(3):
        r.add("tail_plate", E(0.09 - k * 0.02, 0.05, 0.03), "liz_plate", "tail1", loc=(0, -0.10, -0.08 - k * 0.11))

    # --- the spear: a bone head on a bound haft, with a rag on it -------------
    r.limb("shaft", (0, 0.0, 0.70), (0, 0.0, -0.78), 0.032, "spear", "hand_r")
    for k in range(3):
        r.add("binding", E(0.042, 0.042, 0.022), "loincloth", "hand_r", loc=(0, 0, 0.44 + k * 0.09))
    r.limb("head", (0, 0.0, 0.66), (0, 0.0, 1.02), 0.062, "bone", "hand_r", r_tip=0.006)
    for sx in (-1, 1):
        r.limb("barb", (sx * 0.03, 0, 0.72), (sx * 0.10, 0, 0.62), 0.022, "bone", "hand_r", r_tip=0.004)
    r.add("rag", E(0.022, 0.05, 0.065), "rag_red", "hand_r", loc=(0.02, -0.03, 0.54))
    r.add("charm", E(0.025, 0.025, 0.03), "bone", "hand_r", loc=(-0.05, 0.0, 0.30))
    # Modelled at arm's length and scaled up at the end: he stands head and
    # shoulders over the hero, which is the whole point of the redesign.
    r.pose.scale = (1.9, 1.9, 1.9)
    return r


def liz_idle(t):
    s = sn(t)
    return {"_z": 0.01 * s, "chest": X(2 * s), "tail1": (0, 0, 8 * s), "tail2": (0, 0, 10 * sn(t, 0.2)),
            "tail3": (0, 0, 12 * sn(t, 0.35)),
            "shoulder_r": fwd(6), "elbow_r": X(-14), "hand_r": X(4), "head": X(3 * sn(t * 2))}


def liz_walk(t):
    v = gait(t, 34, 26, 22, 0.03, 10)
    v.update({"tail1": (0, 0, 16 * sn(t)), "tail2": (0, 0, 20 * sn(t, 0.25)), "tail3": (0, 0, 24 * sn(t, 0.4)),
              "shoulder_r": fwd(8 + 6 * sn(t)), "elbow_r": X(-16), "hand_r": X(4)})
    return v


def liz_attack(t):
    i, k = phases(t, 0.4, 0.58, 1.0)
    rest = {"shoulder_r": fwd(6), "elbow_r": X(-14), "hand_r": X(4)}
    draw = {"shoulder_r": fwd(-30), "elbow_r": X(-100), "hand_r": X(100), "chest": X(-8), "_y": 0.06,
            "hip_l": fwd(20), "hip_r": fwd(-15), "tail1": (0, 0, -14)}
    # The spear goes out level with the shoulder rather than raised to it: at
    # full reach, pointing straight down the facing, it was longer than an 80px
    # cell is tall in the rows where that facing is towards or away from the
    # camera, and no amount of sliding it about kept both ends inside.
    thrust = {"shoulder_r": fwd(62), "elbow_r": X(-5), "hand_r": X(70), "chest": X(16), "_y": -0.20,
              "hip_l": fwd(40), "hip_r": fwd(-30), "knee_r": X(20), "tail1": (-10, 0, 14)}
    return [mix(rest, draw, k), mix(draw, thrust, k), mix(thrust, rest, k), rest][i]


def liz_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-18 * k), "_y": 0.12 * k, "neck": X(-20 * k), "shoulder_r": fwd(6), "elbow_r": X(-14),
            "hand_r": X(4), "shoulder_l": fwd(-30 * k)}


def liz_death(t):
    # The tail curls in the plane the body falls in. Yawing it about the body's
    # own up axis swung it towards the camera once the body was down, which on
    # a creature this long put the tip in the frame above.
    v = topple(t, armed=True)
    v.update({"tail1": X(26 * ease(t)), "tail2": X(16 * ease(t))})
    return v


# --- ice troll ------------------------------------------------------------------------
def build_troll():
    r = Rig()
    r.joint("pelvis", (0, 0, 0.62))
    r.add("hips", E(0.30, 0.24, 0.20), "troll", "pelvis")
    r.add("kilt", C(0.30, 0.34, 0.22, squash_y=0.8), "hide_grey", "pelvis", loc=(0, 0, 0.02))
    humanoid_legs(r, -0.06, 0.18, 0.28, 0.28, 0.13, "troll", "troll_dk", foot_col="troll_dk")
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(14, 0, 0))
    r.add("torso", E(0.46, 0.34, 0.44), "troll", "chest", loc=(0, 0, 0.36))
    r.add("gut", E(0.30, 0.20, 0.26), "troll_dk", "chest", loc=(0, -0.18, 0.18))
    # Shaggy white fur on the points of the shoulders only. Anything further
    # back is drawn higher on screen from this camera, and a mane down the
    # middle of the back sat exactly where the face should be.
    for s_ in (-1, 1):
        for k in range(3):
            r.limb("mane", (s_ * (0.30 + k * 0.07), 0.06, 0.66 - k * 0.05),
                   (s_ * (0.44 + k * 0.08), 0.20, 0.72 - k * 0.08), 0.07, "troll_fur", "chest", r_tip=0.02)
    humanoid_arms(r, 0.60, 0.48, 0.42, 0.42, 0.16, "troll", "troll_dk", flare=20)
    for side in ("l", "r"):
        r.add("fist", E(0.14, 0.14, 0.14), "troll_dk", "hand_" + side)
    r.joint("neck", (0, -0.16, 0.76), "chest")
    r.joint("head", (0, -0.04, 0.06), "neck", rest=(-20, 0, 0))
    r.add("skull", E(0.22, 0.21, 0.19), "troll", "head", loc=(0, 0, 0.10))
    r.add("jaw", E(0.20, 0.16, 0.10), "troll_dk", "head", loc=(0, -0.10, -0.03))
    r.add("brow", E(0.21, 0.08, 0.05), "troll_dk", "head", loc=(0, -0.16, 0.17))
    r.add("nose", E(0.06, 0.07, 0.06), "troll_dk", "head", loc=(0, -0.22, 0.09))
    for s in (-1, 1):
        r.add("eye", E(0.06, 0.04, 0.05), "troll_eye_glow", "head", loc=(s * 0.10, -0.19, 0.14))
        r.limb("tusk", (s * 0.12, -0.20, -0.04), (s * 0.18, -0.28, 0.14), 0.04, "tusk", "head", r_tip=0.01)
    r.limb("hair", (0, 0.0, 0.26), (0, -0.04, 0.36), 0.07, "troll_fur", "head", r_tip=0.02)
    # A club of blue ice, hanging from the right fist.
    r.limb("club", (0, 0, 0.0), (0, 0, -0.70), 0.07, "ice", "hand_r", r_tip=0.14)
    for k in range(3):
        r.limb("shard", (0, 0, -0.45 - k * 0.1), ((k - 1) * 0.16, -0.08, -0.52 - k * 0.1), 0.045, "ice_dk", "hand_r",
               r_tip=0.005)
    return r


def troll_idle(t):
    s = sn(t)
    return {"_z": 0.015 * s, "chest": X(3 * s), "shoulder_l": fwd(4 * s), "shoulder_r": fwd(-4 * s),
            "head": X(4 * sn(t, 0.3))}


def troll_walk(t):
    v = gait(t, 26, 24, 16, 0.05, 6)
    v["pelvis"] = (0, 8 * sn(t), 0)
    return v


def troll_attack(t):
    i, k = phases(t, 0.45, 0.62, 1.0)
    lift = {"shoulder_r": (-170, -20, 0), "elbow_r": X(-40), "chest": X(-16),
            "_y": 0.08, "hip_l": fwd(16)}
    smash = {"shoulder_r": fwd(40), "elbow_r": X(-5), "chest": X(26), "_y": -0.24, "_z": -0.06,
             "hip_l": fwd(36), "hip_r": fwd(-20), "knee_l": X(24)}
    return [mix({}, lift, k), mix(lift, smash, k), mix(smash, {}, k), {}][i]


def troll_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-14 * k), "_y": 0.10 * k, "neck": X(-16 * k)}


def troll_death(t):
    # It goes over the other way from everything else, and it is big enough to
    # need a longer pull back into the frame.
    k = ease(t * 1.1)
    v = topple(t, dir=-1, armed=True)
    v.update({"chest": X(10 * k), "shoulder_l": fwd(90 * k), "hip_l": fwd(-10 * k)})
    return v


# --- demon --------------------------------------------------------------------------------
def build_demon():
    r = Rig()
    r.joint("pelvis", (0, 0, 0.72))
    r.add("hips", E(0.22, 0.17, 0.15), "demon_dk", "pelvis")
    humanoid_legs(r, -0.05, 0.14, 0.32, 0.36, 0.10, "demon", "demon_dk", digitigrade=True, foot_col="demon_horn")
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(10, 0, 0))
    r.add("torso", E(0.32, 0.22, 0.34), "demon", "chest", loc=(0, 0, 0.32))
    r.add("abs", E(0.16, 0.10, 0.20), "demon_dk", "chest", loc=(0, -0.14, 0.20))
    r.add("core", E(0.06, 0.04, 0.08), "ember_glow", "chest", loc=(0, -0.22, 0.36))
    humanoid_arms(r, 0.54, 0.34, 0.34, 0.34, 0.09, "demon", "demon_dk", flare=16)
    for side in ("l", "r"):
        for k in range(3):
            r.limb("claw", (0, -0.04, -0.06), ((k - 1) * 0.05, -0.10, -0.20), 0.025, "demon_horn", "hand_" + side,
                   r_tip=0.005)
    for s, side in ((-1, "l"), (1, "r")):
        r.joint("wing_" + side, (s * 0.16, 0.18, 0.56), "chest", rest=(0, s * 20, s * 30))
        r.limb("wbone", (0, 0, 0), (s * 0.55, 0.10, 0.42), 0.04, "demon_horn", "wing_" + side, r_tip=0.02)
        r.add("membrane", E(0.30, 0.02, 0.26), "wing_dk", "wing_" + side, loc=(s * 0.30, 0.10, 0.12),
              rot=(0, s * -0.6, 0))
    r.joint("neck", (0, -0.04, 0.66), "chest")
    r.joint("head", (0, -0.02, 0.06), "neck", rest=(-12, 0, 0))
    r.add("skull", E(0.15, 0.15, 0.15), "demon", "head", loc=(0, 0, 0.10))
    r.add("jaw", E(0.12, 0.10, 0.07), "demon_dk", "head", loc=(0, -0.08, -0.01))
    for s in (-1, 1):
        r.add("eye", E(0.04, 0.03, 0.025), "demon_eye_glow", "head", loc=(s * 0.07, -0.13, 0.12))
        r.limb("horn", (s * 0.10, 0.0, 0.20), (s * 0.30, 0.04, 0.30), 0.06, "demon_horn", "head", r_tip=0.04)
        r.limb("horn2", (s * 0.30, 0.04, 0.30), (s * 0.30, -0.08, 0.52), 0.04, "demon_horn", "head", r_tip=0.005)
    r.joint("tail1", (0, 0.14, -0.04), "pelvis", rest=(62, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.45), 0.05, "demon_dk", "tail1", r_tip=0.03)
    r.joint("tail2", (0, 0, -0.45), "tail1", rest=(-30, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.40), 0.03, "demon_dk", "tail2", r_tip=0.015)
    r.add("spade", E(0.08, 0.02, 0.08), "demon_horn", "tail2", loc=(0, 0, -0.44))
    return r


def demon_idle(t):
    s = sn(t)
    return {"_z": 0.015 * s, "chest": X(3 * s), "wing_l": (0, 0, -6 * s), "wing_r": (0, 0, 6 * s),
            "tail1": (0, 0, 10 * s), "shoulder_l": fwd(6), "shoulder_r": fwd(6), "elbow_l": X(-30), "elbow_r": X(-30)}


def demon_walk(t):
    v = gait(t, 30, 30, 22, 0.04, 10)
    v.update({"wing_l": (0, 0, -8 * sn(t * 2)), "wing_r": (0, 0, 8 * sn(t * 2)), "tail1": (0, 0, 16 * sn(t))})
    return v


def demon_attack(t):
    i, k = phases(t, 0.42, 0.6, 1.0)
    raise_ = {"shoulder_l": (-150, 10, 0), "shoulder_r": (-150, -10, 0), "elbow_l": X(-40), "elbow_r": X(-40),
              "chest": X(-12), "wing_l": (0, 0, -40), "wing_r": (0, 0, 40), "_y": 0.06}
    rake = {"shoulder_l": fwd(70), "shoulder_r": fwd(70), "elbow_l": X(-10), "elbow_r": X(-10), "chest": X(26),
            "wing_l": (0, 0, 20), "wing_r": (0, 0, -20), "_y": -0.30, "hip_l": fwd(36), "knee_r": X(20)}
    return [mix({}, raise_, k), mix(raise_, rake, k), mix(rake, {}, k), {}][i]


def demon_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-18 * k), "_y": 0.12 * k, "neck": X(-20 * k), "wing_l": (0, 0, -30 * k), "wing_r": (0, 0, 30 * k)}


def demon_death(t):
    v = topple(t)
    v.update({"wing_l": (0, 0, -40 * ease(t)), "wing_r": (0, 0, 40 * ease(t))})
    return v


# --- imp ------------------------------------------------------------------------------------
def build_imp():
    r = Rig()
    r.joint("pelvis", (0, 0, 0.52))
    r.add("hips", E(0.11, 0.09, 0.08), "imp_dk", "pelvis")
    humanoid_legs(r, -0.02, 0.07, 0.14, 0.16, 0.045, "imp", "imp_dk", digitigrade=True)
    r.joint("chest", (0, 0, 0.04), "pelvis", rest=(18, 0, 0))
    r.add("torso", E(0.13, 0.11, 0.15), "imp", "chest", loc=(0, 0, 0.14))
    humanoid_arms(r, 0.22, 0.13, 0.14, 0.14, 0.04, "imp", "imp_dk", flare=20)
    for s, side in ((-1, "l"), (1, "r")):
        r.joint("wing_" + side, (s * 0.08, 0.08, 0.22), "chest", rest=(0, s * 10, s * 25))
        r.limb("wbone", (0, 0, 0), (s * 0.38, 0.06, 0.26), 0.025, "imp_dk", "wing_" + side, r_tip=0.012)
        r.add("membrane", E(0.20, 0.015, 0.16), "wing_dk", "wing_" + side, loc=(s * 0.20, 0.06, 0.06),
              rot=(0, s * -0.6, 0))
    r.joint("neck", (0, -0.02, 0.28), "chest")
    r.joint("head", (0, 0, 0.02), "neck", rest=(-18, 0, 0))
    r.add("skull", E(0.17, 0.15, 0.15), "imp", "head", loc=(0, 0, 0.12))
    for s in (-1, 1):
        r.add("ear", E(0.10, 0.02, 0.05), "imp_dk", "head", loc=(s * 0.19, 0.02, 0.16), rot=(0, s * -0.5, 0))
        r.add("eye", E(0.04, 0.03, 0.035), "demon_eye_glow", "head", loc=(s * 0.07, -0.13, 0.14))
        r.limb("horn", (s * 0.07, 0, 0.24), (s * 0.12, 0.04, 0.36), 0.03, "demon_horn", "head", r_tip=0.005)
    r.add("grin", E(0.08, 0.03, 0.02), "fang", "head", loc=(0, -0.14, 0.05))
    r.joint("tail1", (0, 0.08, -0.02), "pelvis", rest=(70, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.32), 0.025, "imp_dk", "tail1", r_tip=0.012)
    r.limb("fork", (0, 0.0, 0.30), (0, 0.0, -0.55), 0.02, "iron_dk", "hand_r")
    for k in (-1, 0, 1):
        r.limb("tine", (0, 0, 0.30), (k * 0.06, 0, 0.44), 0.018, "iron_dk", "hand_r", r_tip=0.004)
    return r


def imp_hover(t, flap=40):
    s = sn(t * 2)
    return {"_z": 0.30 + 0.05 * sn(t), "wing_l": (0, 0, -flap * s), "wing_r": (0, 0, flap * s),
            "hip_l": fwd(20), "hip_r": fwd(10), "knee_l": X(30), "knee_r": X(40), "tail1": (0, 0, 16 * sn(t)),
            "shoulder_r": fwd(20), "elbow_r": X(-40), "hand_r": X(30)}


def imp_idle(t):
    return imp_hover(t, 36)


def imp_walk(t):
    v = imp_hover(t, 46)
    v["chest"] = X(14)
    return v


def imp_attack(t):
    i, k = phases(t, 0.4, 0.6, 1.0)
    base = imp_hover(t, 50)
    wind = dict(base, shoulder_r=(-120, 0, 0), elbow_r=X(-30), _y=0.08)
    jab = dict(base, shoulder_r=fwd(80), elbow_r=X(0), hand_r=X(90), _y=-0.30, chest=X(24))
    return [mix(base, wind, k), mix(wind, jab, k), mix(jab, base, k), base][i]


def imp_hurt(t):
    k = math.sin(t * math.pi)
    v = imp_hover(t, 20)
    v.update({"chest": X(-20 * k), "_y": 0.14 * k})
    return v


def imp_death(t):
    # It drops out of its hover first and then goes over sideways like the rest.
    k = ease(t * 1.1)
    v = imp_hover(0, 10 * (1 - k))
    v.update(topple(t, lean=82))
    v.update({"_z": 0.30 * (1 - k) - 0.04 * k,
              "wing_l": (0, 0, -60 * k), "wing_r": (0, 0, 60 * k)})
    return v


# =================================================================================
#  Wyvern
# =================================================================================
def build_wyvern():
    r = Rig()
    r.joint("body", (0, 0.1, 0.95), rest=(-12, 0, 0))
    r.add("torso", E(0.34, 0.56, 0.34), "wyv", "body")
    r.add("belly", E(0.24, 0.48, 0.20), "wyv_belly", "body", loc=(0, -0.02, -0.16))
    for k in range(5):
        r.limb("ridge", (0, -0.34 + k * 0.17, 0.30), (0, -0.28 + k * 0.17, 0.44), 0.05, "wyv_dk", "body", r_tip=0.005)
    # Legs: thick thighs, backward knee, three-toed feet.
    for s, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (s * 0.26, 0.18, -0.12), "body", rest=(12, 0, 0))
        r.limb("thigh", (0, 0, 0), (0, -0.14, -0.42), 0.14, "wyv", "hip_" + side, r_tip=0.09)
        r.joint("knee_" + side, (0, -0.14, -0.42), "hip_" + side, rest=(0, 0, 0))
        r.limb("shin", (0, 0, 0), (0, 0.10, -0.40), 0.08, "wyv_dk", "knee_" + side, r_tip=0.05)
        for k in (-1, 0, 1):
            r.limb("toe", (0, 0.10, -0.40), (k * 0.10, -0.12, -0.46), 0.04, "horn", "knee_" + side, r_tip=0.01)
    # Neck and head, forward and up.
    # About X, a positive pitch leans a limb built upward forward (toward -Y)
    # and swings a hanging one back.
    r.joint("neck1", (0, -0.48, 0.14), "body", rest=(45, 0, 0))
    r.limb("neck", (0, 0, 0), (0, 0, 0.38), 0.15, "wyv", "neck1", r_tip=0.12)
    r.joint("neck2", (0, 0, 0.38), "neck1", rest=(-20, 0, 0))
    r.limb("neck", (0, 0, 0), (0, 0, 0.34), 0.12, "wyv", "neck2", r_tip=0.10)
    r.joint("head", (0, 0, 0.36), "neck2", rest=(-15, 0, 0))
    r.add("skull", E(0.15, 0.24, 0.13), "wyv", "head", loc=(0, -0.10, 0.02))
    r.add("jaw", E(0.11, 0.20, 0.06), "wyv_belly", "head", loc=(0, -0.16, -0.08))
    for s in (-1, 1):
        r.add("eye", E(0.035, 0.035, 0.03), "wyv_eye_glow", "head", loc=(s * 0.10, -0.18, 0.08))
        r.limb("horn", (s * 0.08, 0.06, 0.10), (s * 0.16, 0.34, 0.24), 0.05, "horn", "head", r_tip=0.006)
    # Tail, back and down to a spade.
    r.joint("tail1", (0, 0.54, 0.0), "body", rest=(75, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.50), 0.16, "wyv", "tail1", r_tip=0.10)
    r.joint("tail2", (0, 0, -0.50), "tail1", rest=(8, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.50), 0.10, "wyv", "tail2", r_tip=0.05)
    r.joint("tail3", (0, 0, -0.50), "tail2", rest=(8, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.45), 0.05, "wyv_dk", "tail3", r_tip=0.02)
    r.add("spade", E(0.14, 0.03, 0.12), "wyv_wing", "tail3", loc=(0, 0, -0.50))
    # Wings: an arm out from the shoulder with a sail of membrane under it.
    for s, side in ((-1, "l"), (1, "r")):
        r.joint("wing_" + side, (s * 0.22, -0.20, 0.24), "body", rest=(0, s * -10, 0))
        r.limb("arm", (0, 0, 0), (s * 0.70, 0.20, 0.55), 0.06, "wyv_dk", "wing_" + side, r_tip=0.04)
        r.joint("wrist_" + side, (s * 0.70, 0.20, 0.55), "wing_" + side, rest=(0, s * 40, 0))
        r.limb("finger", (0, 0, 0), (s * 0.80, 0.30, -0.10), 0.04, "wyv_dk", "wrist_" + side, r_tip=0.01)
        r.add("sail1", E(0.46, 0.02, 0.36), "wyv_wing", "wing_" + side, loc=(s * 0.42, 0.28, 0.18),
              rot=(0, s * -0.55, 0))
        r.add("sail2", E(0.44, 0.02, 0.30), "wyv_wing", "wrist_" + side, loc=(s * 0.40, 0.28, -0.12),
              rot=(0, s * 0.25, 0))
    return r


def wyv_idle(t):
    s = sn(t)
    return {"_z": 0.02 * s, "body": X(2 * s), "neck1": X(4 * s), "head": X(-4 * s),
            "wing_l": (0, 0, -6 * s), "wing_r": (0, 0, 6 * s), "tail1": (0, 0, 8 * s), "tail2": (0, 0, 10 * sn(t, 0.2))}


def wyv_walk(t):
    s = sn(t)
    return {"hip_l": fwd(26 * s), "hip_r": fwd(-26 * s), "knee_l": X(20 * max(0, -s)), "knee_r": X(20 * max(0, s)),
            "_z": 0.05 * abs(s), "body": (0, 0, 5 * s), "neck1": (0, 0, -6 * s),
            "wing_l": (0, -14 * sn(t * 2), -12 * sn(t * 2)), "wing_r": (0, 14 * sn(t * 2), 12 * sn(t * 2)),
            "tail1": (0, 0, 14 * s), "tail2": (0, 0, 16 * sn(t, 0.25))}


def wyv_attack(t):
    i, k = phases(t, 0.42, 0.6, 1.0)
    rear = {"body": X(-26), "_z": 0.20, "_y": 0.10, "neck1": X(-25), "head": X(-10),
            "wing_l": (0, 50, -30), "wing_r": (0, -50, 30), "tail1": (18, 0, 0)}
    strike = {"body": X(14), "_z": 0.0, "_y": -0.34, "neck1": X(35), "neck2": X(15), "head": X(15),
              "wing_l": (0, 30, 20), "wing_r": (0, -30, -20), "hip_l": fwd(30), "hip_r": fwd(-10)}
    return [mix({}, rear, k), mix(rear, strike, k), mix(strike, {}, k), {}][i]


def wyv_hurt(t):
    k = math.sin(t * math.pi)
    return {"body": X(-12 * k), "_y": 0.14 * k, "neck1": X(-26 * k), "wing_l": (0, 40 * k, 0), "wing_r": (0, -40 * k, 0)}


def wyv_death(t):
    k = ease(t * 1.1)
    return {"_roll": 70 * k, "_z": -0.40 * k, "neck1": X(40 * k), "head": X(30 * k),
            "wing_l": (0, 60 * k, -30 * k), "wing_r": (0, -20 * k, 40 * k), "hip_l": fwd(40 * k), "tail1": (0, 0, 30 * k)}


# =================================================================================
#  The dragon of the Ice Spire
#
#  Four legs where the wyvern has two, a neck that carries the head above its
#  own wings, and wings half again as wide: at a glance, from across the
#  summit, it has to read as a different order of thing from the wyverns
#  nesting round it. Hoarfrost over glacier blue, with rime growing along the
#  spine and the cold coming out of its mouth.
# =================================================================================

def build_dragon():
    r = Rig()
    r.joint("body", (0, 0.10, 1.10), rest=(-6, 0, 0))
    r.add("torso", E(0.56, 0.92, 0.52), "drake", "body")
    r.add("haunch", E(0.52, 0.40, 0.44), "drake", "body", loc=(0, 0.46, -0.04))
    r.add("belly", E(0.42, 0.78, 0.28), "drake_belly", "body", loc=(0, -0.02, -0.26))
    r.add("chest", E(0.48, 0.38, 0.44), "drake", "body", loc=(0, -0.52, -0.04))
    # Rime along the spine, longest over the shoulders.
    for k in range(7):
        y = -0.56 + k * 0.19
        h = 0.20 + 0.12 * math.cos((k - 2) * 0.9)
        r.limb("ridge", (0, y, 0.40), (0, y - 0.05, 0.40 + h), 0.07, "drake_horn", "body", r_tip=0.006)
    for s2 in (-1, 1):
        r.add("rime", E(0.10, 0.18, 0.15), "rime_glow", "body", loc=(s2 * 0.40, -0.34, 0.26))

    # Four legs, heavy, with a backward knee and three claws. Set wide, so the
    # body is carried on them rather than hung between them.
    for s2, side in ((-1, "l"), (1, "r")):
        for tag, hip_y, thigh, shin, rr in (("fore", -0.46, -0.40, -0.36, 0.15), ("hind", 0.40, -0.46, -0.42, 0.19)):
            j = tag + "_" + side
            r.joint(j, (s2 * 0.38, hip_y, -0.18), "body", rest=(10 if tag == "hind" else 6, 0, 0))
            r.limb("thigh", (0, 0, 0), (0, -0.10, thigh), rr, "drake", j, r_tip=0.11)
            r.joint(j + "_knee", (0, -0.10, thigh), j, rest=(0, 0, 0))
            r.limb("shin", (0, 0, 0), (0, 0.08, shin), 0.10, "drake_dk", j + "_knee", r_tip=0.08)
            r.add("foot", E(0.14, 0.18, 0.07), "drake_dk", j + "_knee", loc=(0, 0.02, shin))
            for k in (-1, 0, 1):
                r.limb("claw", (0, 0.06, shin), (k * 0.13, -0.20, shin - 0.05), 0.045, "drake_horn",
                       j + "_knee", r_tip=0.008)

    # Neck, head and horns. Short and thick: a long thin neck reads as a bird.
    r.joint("neck1", (0, -0.70, 0.20), "body", rest=(46, 0, 0))
    r.limb("neck", (0, 0, 0), (0, 0, 0.44), 0.22, "drake", "neck1", r_tip=0.18)
    r.joint("neck2", (0, 0, 0.44), "neck1", rest=(-34, 0, 0))
    r.limb("neck", (0, 0, 0), (0, 0, 0.36), 0.18, "drake", "neck2", r_tip=0.15)
    r.joint("head", (0, 0, 0.36), "neck2", rest=(-14, 0, 0))
    r.add("skull", E(0.25, 0.34, 0.22), "drake", "head", loc=(0, -0.14, 0.02))
    r.add("snout", E(0.15, 0.30, 0.13), "drake", "head", loc=(0, -0.40, -0.02))
    r.add("jaw", E(0.13, 0.28, 0.08), "drake_belly", "head", loc=(0, -0.38, -0.12))
    r.add("breath", E(0.09, 0.09, 0.08), "rime_glow", "head", loc=(0, -0.58, -0.04))
    for s2 in (-1, 1):
        r.add("eye", E(0.05, 0.05, 0.045), "drake_eye_glow", "head", loc=(s2 * 0.14, -0.26, 0.11))
        # Horns swept back over the neck, and a frill of shorter spines.
        r.limb("horn", (s2 * 0.11, 0.06, 0.14), (s2 * 0.28, 0.54, 0.38), 0.08, "drake_dk", "head", r_tip=0.008)
        for k in range(3):
            r.limb("frill", (s2 * 0.15, 0.10 + k * 0.05, 0.02), (s2 * 0.32, 0.26 + k * 0.07, -0.08 - k * 0.06),
                   0.035, "drake_dk", "head", r_tip=0.004)
        r.limb("fang", (s2 * 0.08, -0.40, -0.08), (s2 * 0.09, -0.42, -0.20), 0.026, "drake_horn", "head", r_tip=0.004)

    # Tail: heavy at the root, tapering to a spiked tip.
    r.joint("tail1", (0, 0.84, 0.02), "body", rest=(74, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.58), 0.22, "drake", "tail1", r_tip=0.16)
    r.joint("tail2", (0, 0, -0.58), "tail1", rest=(10, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.56), 0.16, "drake", "tail2", r_tip=0.10)
    r.joint("tail3", (0, 0, -0.56), "tail2", rest=(10, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.42), 0.12, "drake_dk", "tail3", r_tip=0.04)
    for k in (-1, 1):
        r.limb("barb", (0, 0, -0.34), (k * 0.20, 0, -0.56), 0.05, "drake_horn", "tail3", r_tip=0.006)
        r.limb("barb", (0, 0, -0.46), (k * 0.13, 0, -0.66), 0.04, "drake_horn", "tail3", r_tip=0.005)

    # Wings, folded along the back at rest: the arm goes up and back from the
    # shoulder and the fingers carry the membrane down the body toward the
    # tail. A wing built spread stands up over the back like a pair of ears
    # from this camera; the poses swing these out instead, about Z.
    for s2, side in ((-1, "l"), (1, "r")):
        r.joint("wing_" + side, (s2 * 0.34, -0.34, 0.34), "body", rest=(0, 0, 0))
        r.limb("arm", (0, 0, 0), (s2 * 0.24, 0.18, 0.36), 0.09, "drake_dk", "wing_" + side, r_tip=0.06)
        r.joint("wrist_" + side, (s2 * 0.24, 0.18, 0.36), "wing_" + side, rest=(0, 0, 0))
        # Fingers sweeping back and down the flank, with the membrane hanging
        # between them: folded, not spread. A sail built over the back reads
        # from this camera as a balloon tied to the spine.
        r.limb("finger", (0, 0, 0), (s2 * 0.10, 0.96, -0.34), 0.05, "drake_dk", "wrist_" + side, r_tip=0.010)
        r.limb("finger", (0, 0, 0), (s2 * 0.30, 0.74, -0.40), 0.042, "drake_dk", "wrist_" + side, r_tip=0.009)
        r.limb("finger", (0, 0, 0), (s2 * 0.48, 0.32, -0.42), 0.036, "drake_dk", "wrist_" + side, r_tip=0.008)
        r.add("sail1", E(0.05, 0.46, 0.34), "drake_wing", "wrist_" + side, loc=(s2 * 0.16, 0.48, -0.34),
              rot=(0, s2 * 0.18, 0))
        r.add("sail2", E(0.05, 0.34, 0.28), "drake_wing", "wrist_" + side, loc=(s2 * 0.32, 0.46, -0.40),
              rot=(0, s2 * 0.30, 0))
        r.limb("thumb", (0, 0, 0), (s2 * 0.18, -0.12, 0.20), 0.035, "drake_horn", "wrist_" + side, r_tip=0.005)
    return r


def drake_idle(t):
    s = sn(t)
    return {"_z": 0.03 * s, "body": X(2 * s), "neck1": X(5 * s), "neck2": X(-3 * s), "head": X(-5 * s),
            "wing_l": (0, 0, -5 * s), "wing_r": (0, 0, 5 * s), "wrist_l": (0, 4 * s, 0), "wrist_r": (0, -4 * s, 0),
            "tail1": (0, 0, 7 * s), "tail2": (0, 0, 9 * sn(t, 0.2)), "tail3": (0, 0, 11 * sn(t, 0.35))}


def drake_walk(t):
    s = sn(t)
    # A four-legged walk: each foreleg moves with the opposite hind.
    return {"fore_l": fwd(22 * s), "hind_r": fwd(20 * s), "fore_r": fwd(-22 * s), "hind_l": fwd(-20 * s),
            "fore_l_knee": X(16 * max(0, -s)), "fore_r_knee": X(16 * max(0, s)),
            "hind_l_knee": X(18 * max(0, s)), "hind_r_knee": X(18 * max(0, -s)),
            "_z": 0.04 * abs(s), "body": (0, 0, 4 * s), "neck1": (0, 0, -5 * s), "head": (0, 0, 3 * s),
            "wing_l": (0, 0, -14 - 8 * sn(t * 2)), "wing_r": (0, 0, 14 + 8 * sn(t * 2)),
            "tail1": (0, 0, 12 * s), "tail2": (0, 0, 14 * sn(t, 0.25))}


def drake_attack(t):
    """Rears up on the hind legs with the wings thrown wide, then comes down
    with the head: the breath is the glow in its mouth as it does."""
    i, k = phases(t, 0.40, 0.62, 1.0)
    rear = {"body": X(-30), "_z": 0.26, "_y": 0.14, "neck1": X(-28), "neck2": X(-10), "head": X(-14),
            "fore_l": fwd(48), "fore_r": fwd(44), "fore_l_knee": X(30), "fore_r_knee": X(30),
            "wing_l": (-20, 0, -96), "wing_r": (-20, 0, 96), "wrist_l": (0, 40, 0), "wrist_r": (0, -40, 0),
            "tail1": (16, 0, 0)}
    strike = {"body": X(16), "_z": -0.02, "_y": -0.40, "neck1": X(38), "neck2": X(20), "head": X(20),
              "fore_l": fwd(-10), "fore_r": fwd(-6),
              "wing_l": (10, 0, -60), "wing_r": (10, 0, 60), "hind_l": fwd(22), "hind_r": fwd(-8)}
    return [mix({}, rear, k), mix(rear, strike, k), mix(strike, {}, k), {}][i]


def drake_hurt(t):
    k = math.sin(t * math.pi)
    return {"body": X(-14 * k), "_y": 0.16 * k, "neck1": X(-30 * k), "head": X(-12 * k),
            "wing_l": (0, 0, -70 * k), "wing_r": (0, 0, 70 * k), "tail1": (0, 0, 20 * k)}


def drake_death(t):
    k = ease(t * 1.1)
    return {"_roll": 76 * k, "_z": -0.46 * k, "neck1": X(44 * k), "neck2": X(20 * k), "head": X(34 * k),
            "wing_l": (0, 0, -100 * k), "wing_r": (0, 0, 40 * k), "wrist_l": (0, 50 * k, 0),
            "fore_l": fwd(46 * k), "hind_l": fwd(40 * k), "tail1": (0, 0, 34 * k)}


# =================================================================================
#  Elder Vask
#
#  The only NPC in the game with art of its own rather than a townsfolk sheet,
#  because he is the only one who never stands up: an old man in a rocking
#  chair in the guild hall, white-bearded, stooped, a blanket over his knees
#  and his stick across them. The chair is part of the rig rather than a prop
#  beside him -- a chair he is not actually sitting in reads as furniture he
#  happens to be standing next to, and a prop cannot rock. Everything hangs off
#  a joint down at the rockers, so tilting that one joint rocks the man and the
#  chair together, which is the whole animation.
# =================================================================================

def build_vask():
    r = Rig()
    r.joint("rock", (0, 0.04, 0.05), rest=(-2, 0, 0))

    # --- the chair -----------------------------------------------------------------
    for sx in (-1, 1):
        # A rocker: a long shallow arc, faked as a flattened ellipsoid.
        r.add("rocker", E(0.035, 0.34, 0.05), "chair_wood_dk", "rock", loc=(sx * 0.20, 0.02, -0.03))
        # Legs up to the seat, and the front posts that carry the arms.
        for sy, h in ((-0.20, 0.30), (0.18, 0.30)):
            r.limb("leg", (sx * 0.20, sy, -0.02), (sx * 0.18, sy, h), 0.028, "chair_wood", "rock", r_tip=0.024)
        r.limb("post", (sx * 0.19, -0.20, 0.30), (sx * 0.19, -0.20, 0.50), 0.025, "chair_wood", "rock", r_tip=0.022)
        # The arm rest, and the back upright behind it.
        r.add("arm", E(0.035, 0.24, 0.028), "chair_wood_lt", "rock", loc=(sx * 0.19, -0.02, 0.52))
        r.limb("upright", (sx * 0.19, 0.18, 0.30), (sx * 0.17, 0.22, 0.92), 0.028, "chair_wood", "rock", r_tip=0.022)
    r.add("seat", E(0.21, 0.19, 0.028), "chair_wood_lt", "rock", loc=(0, -0.01, 0.31))
    r.add("cushion", E(0.18, 0.16, 0.035), "vask_blanket", "rock", loc=(0, -0.02, 0.335))
    for k in range(4):
        r.add("slat", E(0.14, 0.02, 0.035), "chair_wood", "rock", loc=(0, 0.20 + k * 0.006, 0.46 + k * 0.14))
    r.add("rail", E(0.19, 0.03, 0.04), "chair_wood_lt", "rock", loc=(0, 0.22, 0.94))
    for sx in (-1, 1):
        r.add("finial", E(0.035, 0.035, 0.045), "chair_wood_lt", "rock", loc=(sx * 0.17, 0.22, 0.97))

    # --- the man in it --------------------------------------------------------------
    # Sitting: the thighs run forward out of the hips and the shins drop from
    # the knees, so he is folded into the chair rather than standing in it.
    r.joint("pelvis", (0, -0.02, 0.38), "rock")
    r.add("hips", E(0.15, 0.13, 0.10), "vask_robe", "pelvis")
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.09, -0.02, 0.0), "pelvis")
        r.limb("thigh", (0, 0, 0), (0, -0.22, -0.02), 0.075, "vask_robe", "hip_" + side, r_tip=0.065)
        r.joint("knee_" + side, (0, -0.22, -0.02), "hip_" + side)
        r.limb("shin", (0, 0, 0), (0, -0.02, -0.30), 0.06, "vask_robe_dk", "knee_" + side, r_tip=0.05)
        r.add("boot", E(0.06, 0.09, 0.045), "boot", "knee_" + side, loc=(0, -0.06, -0.32))
    # The blanket over his knees.
    r.add("blanket", E(0.20, 0.20, 0.055), "vask_blanket", "pelvis", loc=(0, -0.16, 0.03))
    r.add("blanket_fold", E(0.19, 0.06, 0.07), "vask_blanket", "pelvis", loc=(0, -0.28, -0.02))

    # Stooped: the chest leans forward and the neck carries the head further
    # forward still, which is most of what makes him read as old.
    r.joint("chest", (0, 0.01, 0.10), "pelvis", rest=(16, 0, 0))
    r.add("torso", E(0.17, 0.13, 0.21), "vask_robe", "chest", loc=(0, 0, 0.18))
    r.add("shawl", E(0.21, 0.17, 0.09), "vask_shawl", "chest", loc=(0, 0.01, 0.30))
    r.add("shawl_front", E(0.10, 0.06, 0.14), "vask_shawl", "chest", loc=(0, -0.10, 0.24))

    # Arms hanging to the rests, forearms forward over the blanket, hands on
    # the stick lying across his knees.
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.16, 0, 0.32), "chest", rest=(6, sx * -8, 0))
        r.limb("upper", (0, 0, 0), (0, 0, -0.20), 0.05, "vask_robe", "shoulder_" + side, r_tip=0.045)
        r.joint("elbow_" + side, (0, 0, -0.20), "shoulder_" + side, rest=(-72, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.20), 0.045, "vask_robe_dk", "elbow_" + side, r_tip=0.04)
        r.joint("hand_" + side, (0, 0, -0.20), "elbow_" + side)
        r.add("hand", E(0.045, 0.05, 0.04), "vask_skin_dk", "hand_" + side)
    # The stick, across the knees and under both hands.
    r.add("stick", C(0.018, 0.018, 0.46), "vask_stick", "pelvis", loc=(0, -0.20, 0.10),
          rot=(0, math.radians(90), 0))
    r.add("stick_knob", E(0.035, 0.035, 0.035), "vask_stick", "pelvis", loc=(-0.24, -0.20, 0.10))

    r.joint("neck", (0, -0.03, 0.38), "chest", rest=(-10, 0, 0))
    r.add("neckp", C(0.05, 0.055, 0.07), "vask_skin_dk", "neck", loc=(0, 0, 0.05))
    r.joint("head", (0, -0.01, 0.09), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.115, 0.12, 0.115), "vask_skin", "head", loc=(0, 0, 0.06))
    # Bald on top with white hair round the sides and back, and a big beard.
    r.add("fringe", E(0.125, 0.125, 0.06), "vask_hair", "head", loc=(0, 0.02, 0.05))
    r.add("hair_back", E(0.115, 0.07, 0.10), "vask_hair", "head", loc=(0, 0.08, 0.04))
    for sx in (-1, 1):
        r.add("hair_side", E(0.035, 0.07, 0.07), "vask_hair", "head", loc=(sx * 0.11, 0.01, 0.03))
        r.add("brow", E(0.045, 0.02, 0.022), "vask_hair", "head", loc=(sx * 0.055, -0.10, 0.09))
        r.add("eye", E(0.018, 0.012, 0.014), "eye", "head", loc=(sx * 0.05, -0.105, 0.06))
    r.add("nose", E(0.028, 0.035, 0.035), "vask_skin", "head", loc=(0, -0.115, 0.03))
    r.add("beard", E(0.10, 0.075, 0.105), "vask_hair", "head", loc=(0, -0.06, -0.09))
    r.add("beard_tip", E(0.06, 0.05, 0.07), "vask_hair_dk", "head", loc=(0, -0.05, -0.18))
    r.add("moustache", E(0.065, 0.03, 0.028), "vask_hair", "head", loc=(0, -0.10, -0.015))
    # Modelled small and scaled up at the end: a man in a chair is about two
    # and a half units tall next to the hero, and building him at that size
    # would mean writing every number twice as long.
    r.pose.scale = (2.4, 2.4, 2.4)
    return r


def vask_idle(t):
    """The chair rocks, slowly, and he breathes. Nothing else happens: that is
    the character."""
    s = sn(t)
    return {"rock": X(3.5 * s), "chest": X(1.5 * sn(t, 0.1)), "head": X(-2.0 * sn(t, 0.15)),
            "neck": X(1.0 * sn(t, 0.2))}


def vask_walk(t):
    # He does not. The clip exists so anything that asks for it gets the man in
    # his chair rather than nothing at all.
    return vask_idle(t)


# =================================================================================
#  The dead of Hollowrest
#
#  Three of them, and they have to be told apart at a glance across a dark
#  graveyard: the zombie is a thick shambling thing with its arms out, the
#  skeleton is a thin bright one with a rusted blade, and the wraith is a
#  hooded robe with nothing in it and no feet on the ground.
# =================================================================================

def build_zombie():
    r = Rig()
    r.joint("pelvis", (0, 0, 0.60))
    r.add("hips", E(0.17, 0.14, 0.12), "rot_cloth", "pelvis")
    humanoid_legs(r, -0.04, 0.11, 0.26, 0.26, 0.075, "rot_cloth", "rot_cloth_dk", foot_col="rot_dk")
    # Stooped, with one shoulder lower than the other: nothing about it is level.
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(22, 0, 4))
    r.add("torso", E(0.20, 0.15, 0.26), "rot", "chest", loc=(0, 0, 0.22))
    r.add("shirt", E(0.21, 0.16, 0.16), "rot_cloth", "chest", loc=(0, 0.01, 0.12))
    # A wound in the side with a rib showing through it.
    r.add("wound", E(0.07, 0.05, 0.08), "rot_wound", "chest", loc=(0.11, -0.10, 0.26))
    for k in range(2):
        r.limb("rib", (-0.10, -0.12, 0.20 + k * 0.07), (0.04, -0.13, 0.22 + k * 0.07), 0.018, "bone_w",
               "chest", r_tip=0.012)
    humanoid_arms(r, 0.40, 0.21, 0.24, 0.23, 0.06, "rot", "rot_dk", flare=16)
    for side in ("l", "r"):
        for k in (-1, 0, 1):
            r.limb("finger", (k * 0.035, -0.02, -0.05), (k * 0.05, -0.09, -0.08), 0.015, "rot_dk",
                   "hand_" + side, r_tip=0.004)
    r.joint("neck", (0, -0.03, 0.48), "chest", rest=(10, 0, 0))
    r.joint("head", (0, -0.01, 0.08), "neck", rest=(-16, 0, 0))
    r.add("skull", E(0.115, 0.12, 0.125), "rot", "head", loc=(0, 0, 0.08))
    r.add("hair", E(0.115, 0.11, 0.06), "rot_cloth_dk", "head", loc=(0, 0.03, 0.14))
    # The jaw hangs: the single clearest thing that says this one is dead.
    r.add("jaw", E(0.075, 0.08, 0.05), "rot_dk", "head", loc=(0, -0.07, -0.03))
    r.add("mouth", E(0.05, 0.04, 0.035), "rot_cloth_dk", "head", loc=(0, -0.08, 0.01))
    for sx in (-1, 1):
        r.add("socket", E(0.032, 0.025, 0.03), "rot_dk", "head", loc=(sx * 0.055, -0.095, 0.10))
    r.add("eye", E(0.022, 0.018, 0.02), "rot_eye_glow", "head", loc=(-0.055, -0.105, 0.10))
    r.pose.scale = (1.45, 1.45, 1.45)
    return r


def zom_idle(t):
    s = sn(t)
    return {"_z": 0.012 * s, "chest": (2 * s, 0, 4), "neck": X(2 * s), "head": (0, 0, 6 * sn(t, 0.3)),
            "shoulder_l": fwd(58 + 4 * s), "elbow_l": X(-26), "shoulder_r": fwd(52 - 4 * s), "elbow_r": X(-34)}


def zom_walk(t):
    # A shamble: one leg drags, so the swing is lopsided and the body rolls.
    s = sn(t)
    return {"hip_l": fwd(26 * s), "hip_r": fwd(-14 * s), "knee_l": X(20 * max(0.0, -s)), "knee_r": X(8),
            "_z": 0.02 * abs(s), "chest": (18, 0, 5 * s), "neck": X(6),
            "shoulder_l": fwd(60), "elbow_l": X(-24), "shoulder_r": fwd(54), "elbow_r": X(-32),
            "head": (0, 0, 8 * sn(t, 0.2))}


def zom_attack(t):
    i, k = phases(t, 0.38, 0.6, 1.0)
    rest = {"shoulder_l": fwd(58), "elbow_l": X(-26), "shoulder_r": fwd(52), "elbow_r": X(-34), "chest": (20, 0, 4)}
    rear = {"shoulder_r": fwd(-24), "elbow_r": X(-70), "chest": (6, 0, -12), "neck": X(-10), "_y": 0.06}
    swipe = {"shoulder_r": fwd(78), "elbow_r": X(-10), "chest": (30, 0, 16), "neck": X(12), "_y": -0.20,
             "hip_l": fwd(22), "shoulder_l": fwd(40)}
    return [mix(rest, rear, k), mix(rear, swipe, k), mix(swipe, rest, k), rest][i]


def zom_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": (20 - 24 * k, 0, 4), "_y": 0.12 * k, "neck": X(-18 * k),
            "shoulder_l": fwd(58 - 30 * k), "shoulder_r": fwd(52 - 26 * k)}


def zom_death(t):
    v = topple(t)
    v.update({"shoulder_l": fwd(40 * (1 - ease(t))), "shoulder_r": fwd(36 * (1 - ease(t)))})
    return v


def build_skeleton():
    r = Rig()
    r.joint("pelvis", (0, 0, 0.60))
    r.add("hips", E(0.13, 0.10, 0.09), "bone_g", "pelvis")
    humanoid_legs(r, -0.03, 0.085, 0.27, 0.27, 0.042, "bone_w", "bone_g", foot_col="bone_g")
    r.joint("chest", (0, 0, 0.07), "pelvis", rest=(6, 0, 0))
    # A spine with a cage of ribs on it rather than a torso.
    r.limb("spine", (0, 0.02, 0), (0, 0.02, 0.34), 0.035, "bone_g", "chest", r_tip=0.03)
    for k in range(4):
        wide = 0.145 - abs(k - 1) * 0.015
        r.add("rib", E(wide, 0.075, 0.022), "bone_w", "chest", loc=(0, -0.01, 0.08 + k * 0.075))
    r.add("sternum", E(0.035, 0.05, 0.14), "bone_w", "chest", loc=(0, -0.07, 0.17))
    r.add("collar", E(0.16, 0.05, 0.03), "bone_w", "chest", loc=(0, -0.02, 0.36))
    humanoid_arms(r, 0.36, 0.17, 0.23, 0.22, 0.035, "bone_w", "bone_g", flare=14)
    for side in ("l", "r"):
        for k in (-1, 0, 1):
            r.limb("finger", (k * 0.025, -0.02, -0.03), (k * 0.04, -0.07, -0.06), 0.011, "bone_w",
                   "hand_" + side, r_tip=0.003)
    r.joint("neck", (0, 0, 0.42), "chest")
    r.limb("vertebra", (0, 0.01, 0), (0, 0.01, 0.07), 0.028, "bone_g", "neck", r_tip=0.026)
    r.joint("head", (0, 0, 0.08), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.10, 0.105, 0.105), "bone_w", "head", loc=(0, 0, 0.07))
    r.add("brow", E(0.10, 0.05, 0.03), "bone_g", "head", loc=(0, -0.07, 0.11))
    r.add("jaw", E(0.075, 0.075, 0.035), "bone_w", "head", loc=(0, -0.05, -0.01))
    for sx in (-1, 1):
        r.add("socket", E(0.032, 0.02, 0.032), "bone_dk", "head", loc=(sx * 0.045, -0.085, 0.08))
        r.add("spark", E(0.016, 0.012, 0.016), "rot_eye_glow", "head", loc=(sx * 0.045, -0.095, 0.08))
        for k in range(3):
            r.limb("tooth", (sx * (0.015 + k * 0.018), -0.075, 0.015), (sx * (0.015 + k * 0.018), -0.075, -0.005),
                   0.008, "bone_w", "head", r_tip=0.003)
    # A notched, rusted sword in the right hand, and a broken buckler on the left.
    r.limb("blade", (0, 0, -0.02), (0, 0, 0.54), 0.035, "rust", "hand_r", r_tip=0.012)
    r.add("edge", E(0.012, 0.012, 0.22), "rust_lt", "hand_r", loc=(0.02, 0, 0.30))
    r.add("guard", E(0.09, 0.03, 0.02), "bone_g", "hand_r", loc=(0, 0, -0.02))
    r.add("buckler", E(0.13, 0.04, 0.13), "rust", "hand_l", loc=(0, -0.04, -0.02))
    r.add("boss", E(0.045, 0.03, 0.045), "rust_lt", "hand_l", loc=(0, -0.07, -0.02))
    r.pose.scale = (1.4, 1.4, 1.4)
    return r


def skel_idle(t):
    s = sn(t)
    return {"_z": 0.01 * s, "chest": X(2 * s), "head": (0, 0, 5 * sn(t, 0.25)),
            "shoulder_r": fwd(14), "elbow_r": X(-58), "hand_r": X(20),
            "shoulder_l": fwd(20), "elbow_l": X(-64)}


def skel_walk(t):
    v = gait(t, 30, 26, 18, 0.025, 6)
    v.update({"shoulder_r": fwd(16 + 6 * sn(t)), "elbow_r": X(-58), "hand_r": X(20),
              "shoulder_l": fwd(22), "elbow_l": X(-64)})
    return v


def skel_attack(t):
    i, k = phases(t, 0.36, 0.56, 1.0)
    rest = {"shoulder_r": fwd(14), "elbow_r": X(-58), "hand_r": X(20), "shoulder_l": fwd(20), "elbow_l": X(-64)}
    raise_ = {"shoulder_r": fwd(-74), "elbow_r": X(-96), "hand_r": X(30), "chest": (-6, 0, -14), "_y": 0.05}
    chop = {"shoulder_r": fwd(64), "elbow_r": X(-12), "hand_r": X(10), "chest": (18, 0, 16), "_y": -0.20,
            "hip_l": fwd(24), "hip_r": fwd(-16)}
    return [mix(rest, raise_, k), mix(raise_, chop, k), mix(chop, rest, k), rest][i]


def skel_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-16 * k), "_y": 0.12 * k, "neck": X(-16 * k), "head": (0, 0, 12 * k),
            "shoulder_l": fwd(20 - 40 * k), "shoulder_r": fwd(14), "elbow_r": X(-58)}


def skel_death(t):
    # It comes apart rather than falling over: the pieces drop where they stood.
    k = ease(t)
    return {"_z": -0.34 * k, "_pitch": 24 * k, "chest": X(30 * k), "neck": X(40 * k), "head": (30 * k, 0, 40 * k),
            "shoulder_l": (0, 0, -70 * k), "shoulder_r": (0, 0, 70 * k), "elbow_l": X(-80 * k),
            "hip_l": fwd(-50 * k), "hip_r": fwd(40 * k), "knee_l": X(70 * k), "knee_r": X(60 * k)}


def build_wraith():
    """A robe with nothing in it: no feet, a hood with two lights in it, and
    hands of bone at the ends of the sleeves."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.78))
    # The robe: a wide skirt tapering to a wisp instead of legs.
    r.add("skirt", C(0.24, 0.05, 0.52, squash_y=0.85), "shroud", "pelvis", loc=(0, 0.02, -0.18))
    for k in range(3):
        r.add("tatter", E(0.07 - k * 0.012, 0.05, 0.10), "shroud_dk", "pelvis",
              loc=(-0.14 + k * 0.14, -0.02, -0.46 - k * 0.04))
    r.add("wisp", E(0.07, 0.06, 0.09), "wisp_glow", "pelvis", loc=(0, 0.02, -0.52))
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(6, 0, 0))
    r.add("robe", E(0.22, 0.17, 0.24), "shroud", "chest", loc=(0, 0, 0.16))
    r.add("mantle", E(0.25, 0.20, 0.10), "shroud_lt", "chest", loc=(0, 0.01, 0.30))
    r.add("clasp", E(0.04, 0.03, 0.04), "grave_gold", "chest", loc=(0, -0.14, 0.30))
    # Sleeves, with skeletal hands hanging out of them.
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.20, 0, 0.30), "chest", rest=(0, sx * -18, 0))
        r.limb("sleeve", (0, 0, 0), (0, 0, -0.26), 0.075, "shroud", "shoulder_" + side, r_tip=0.055)
        r.joint("elbow_" + side, (0, 0, -0.26), "shoulder_" + side, rest=(-30, 0, 0))
        r.limb("cuff", (0, 0, 0), (0, 0, -0.18), 0.055, "shroud_dk", "elbow_" + side, r_tip=0.04)
        r.joint("hand_" + side, (0, 0, -0.18), "elbow_" + side)
        r.add("palm", E(0.045, 0.04, 0.05), "bone_w", "hand_" + side)
        for k in (-1, 0, 1):
            r.limb("finger", (k * 0.03, -0.02, -0.03), (k * 0.05, -0.07, -0.07), 0.012, "bone_w",
                   "hand_" + side, r_tip=0.003)
    # The hood, with the dark inside it and two lights where a face is not.
    r.joint("neck", (0, 0, 0.44), "chest")
    r.joint("head", (0, -0.01, 0.06), "neck", rest=(-10, 0, 0))
    r.add("hood", E(0.135, 0.145, 0.15), "shroud_lt", "head", loc=(0, 0.01, 0.08))
    r.add("hood_peak", E(0.07, 0.09, 0.09), "shroud_lt", "head", loc=(0, 0.06, 0.18))
    r.add("dark", E(0.095, 0.05, 0.10), "shroud_dk", "head", loc=(0, -0.10, 0.07))
    for sx in (-1, 1):
        r.add("light", E(0.026, 0.02, 0.026), "wisp_glow", "head", loc=(sx * 0.045, -0.13, 0.09))
    r.pose.scale = (1.3, 1.3, 1.3)
    return r


def wraith_idle(t):
    s = sn(t)
    # It does not stand: it hangs, and drifts.
    return {"_z": 0.05 * s, "chest": X(3 * s), "head": (0, 0, 7 * sn(t, 0.3)),
            "shoulder_l": (0, -10 - 6 * s, 0), "shoulder_r": (0, 10 + 6 * s, 0), "pelvis": (0, 0, 4 * s)}


def wraith_walk(t):
    s = sn(t)
    return {"_z": 0.07 * s, "_y": 0.02 * sn(t, 0.25), "chest": X(5 * s),
            "shoulder_l": (0, -16 - 10 * s, 0), "shoulder_r": (0, 16 + 10 * s, 0),
            "head": (0, 0, 10 * sn(t, 0.2))}


def wraith_attack(t):
    i, k = phases(t, 0.40, 0.58, 1.0)
    rest = {"shoulder_l": (0, -10, 0), "shoulder_r": (0, 10, 0)}
    gather = {"_z": 0.14, "chest": X(-12), "shoulder_l": fwd(-40), "shoulder_r": fwd(-40),
              "elbow_l": X(-70), "elbow_r": X(-70)}
    reach = {"_z": 0.02, "_y": -0.26, "chest": X(20), "shoulder_l": fwd(84), "shoulder_r": fwd(84),
             "elbow_l": X(-6), "elbow_r": X(-6), "head": X(10)}
    return [mix(rest, gather, k), mix(gather, reach, k), mix(reach, rest, k), rest][i]


def wraith_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-20 * k), "_y": 0.14 * k, "_z": 0.06 * k,
            "shoulder_l": (0, -30 * k, 0), "shoulder_r": (0, 30 * k, 0)}


def wraith_death(t):
    # It sinks and folds in on itself instead of falling.
    k = ease(t)
    return {"_z": -0.46 * k, "chest": X(40 * k), "neck": X(30 * k), "head": X(20 * k),
            "shoulder_l": fwd(-30 * k), "shoulder_r": fwd(-30 * k), "_roll": 12 * k}


# =================================================================================
#  What is down the well
# =================================================================================

def build_slime():
    """A blob with something undigested in the middle of it. Everything about
    it is one shape, so all of the character has to come from how it moves."""
    r = Rig()
    r.joint("body", (0, 0, 0.16))
    blob = r.add("blob", E(0.30, 0.30, 0.22), "slime", "body")
    r.add("sheen", E(0.20, 0.20, 0.13), "slime_lt", "body", loc=(-0.05, -0.08, 0.06))
    r.add("core", E(0.09, 0.09, 0.07), "slime_core", "body", loc=(0, -0.02, -0.02))
    r.add("foot", E(0.32, 0.30, 0.06), "slime_dk", "body", loc=(0, 0, -0.16))
    # Eyes big enough to survive a 48px frame in a dark room, and a pair on the
    # back of it too: a blob seen from behind is otherwise a green dot, and the
    # thing the player needs to read at a glance is which way it is looking.
    for sx in (-1, 1):
        r.add("eye", E(0.075, 0.06, 0.075), "slime_eye", "body", loc=(sx * 0.11, -0.235, 0.07))
        r.add("pupil", E(0.038, 0.03, 0.038), "slime_pupil", "body", loc=(sx * 0.11, -0.285, 0.07))
        r.add("eye_back", E(0.055, 0.045, 0.055), "slime_eye", "body", loc=(sx * 0.11, 0.235, 0.07))
        r.add("pupil_back", E(0.028, 0.022, 0.028), "slime_pupil", "body", loc=(sx * 0.11, 0.285, 0.07))
    # A drip running off one side, and a blob of it on the floor.
    r.add("drip", E(0.05, 0.05, 0.10), "slime", "body", loc=(0.26, 0.06, -0.10))
    r.pose.scale = (1.75, 1.75, 1.75)
    return r


def slime_idle(t):
    s = sn(t)
    # It breathes by squashing: the only thing it can do standing still.
    return {"_z": 0.02 * s, "body": (3 * s, 0, 4 * sn(t, 0.25))}


def slime_walk(t):
    s = sn(t)
    return {"_z": 0.05 * max(0.0, s), "_y": -0.02 * s, "body": (8 * s, 0, 6 * sn(t, 0.2))}


def slime_attack(t):
    i, k = phases(t, 0.36, 0.56, 1.0)
    gather = {"_z": -0.06, "body": (-14, 0, 0)}
    leap = {"_z": 0.22, "_y": -0.26, "body": (18, 0, 0)}
    return [mix({}, gather, k), mix(gather, leap, k), mix(leap, {}, k), {}][i]


def slime_hurt(t):
    k = math.sin(t * math.pi)
    return {"_z": -0.06 * k, "body": (-20 * k, 0, 14 * k), "_y": 0.10 * k}


def slime_death(t):
    # It does not fall over: it goes flat.
    k = ease(t)
    return {"_z": -0.13 * k, "body": (-40 * k, 0, 0), "_pitch": 10 * k}


def build_bat():
    r = Rig()
    r.joint("body", (0, 0, 0.62), rest=(16, 0, 0))
    r.add("torso", E(0.085, 0.13, 0.10), "bat_fur", "body")
    r.add("belly", E(0.06, 0.09, 0.06), "bat_fur_dk", "body", loc=(0, -0.04, -0.05))
    r.joint("head", (0, -0.12, 0.04), "body", rest=(-14, 0, 0))
    r.add("skull", E(0.065, 0.07, 0.065), "bat_fur", "head")
    r.add("snout", E(0.035, 0.05, 0.03), "bat_fur_dk", "head", loc=(0, -0.07, -0.02))
    for sx in (-1, 1):
        r.add("ear", E(0.028, 0.012, 0.075), "bat_fur_dk", "head", loc=(sx * 0.05, 0.01, 0.10),
              rot=(0, sx * -0.3, 0))
        r.add("eye", E(0.018, 0.014, 0.016), "bat_eye_glow", "head", loc=(sx * 0.035, -0.06, 0.02))
        r.limb("fang", (sx * 0.02, -0.09, -0.03), (sx * 0.02, -0.09, -0.06), 0.009, "ankou_bone", "head",
               r_tip=0.002)
        # Wings: an arm out to a wrist, three fingers, membrane between.
        r.joint("wing_" + ("l" if sx < 0 else "r"), (sx * 0.07, 0, 0.03), "body", rest=(0, sx * -20, 0))
        w = "wing_" + ("l" if sx < 0 else "r")
        r.limb("arm", (0, 0, 0), (sx * 0.20, 0.02, 0.10), 0.018, "bat_fur_dk", w, r_tip=0.012)
        r.joint("wrist_" + ("l" if sx < 0 else "r"), (sx * 0.20, 0.02, 0.10), w, rest=(0, sx * 34, 0))
        wr = "wrist_" + ("l" if sx < 0 else "r")
        for k, (dx, dy, dz) in enumerate(((0.26, 0.06, -0.02), (0.20, 0.16, -0.10), (0.10, 0.22, -0.14))):
            r.limb("finger", (0, 0, 0), (sx * dx, dy, dz), 0.012 - k * 0.002, "bat_fur_dk", wr, r_tip=0.004)
        r.add("membrane", E(0.16, 0.02, 0.12), "bat_wing", wr, loc=(sx * 0.14, 0.12, -0.06),
              rot=(0, sx * 0.4, 0))
        r.add("membrane_in", E(0.11, 0.02, 0.09), "bat_wing", w, loc=(sx * 0.11, 0.06, 0.04),
              rot=(0, sx * -0.3, 0))
    r.joint("feet", (0, 0.06, -0.09), "body")
    for sx in (-1, 1):
        r.limb("leg", (sx * 0.03, 0, 0), (sx * 0.04, 0.03, -0.10), 0.012, "bat_fur_dk", "feet", r_tip=0.004)
    r.pose.scale = (1.5, 1.5, 1.5)
    return r


def _bat_flap(t, amp=44.0, speed=2.0):
    s = sn(t * speed)
    return {"wing_l": (0, -amp * s, 0), "wing_r": (0, amp * s, 0),
            "wrist_l": (0, 24 * s, 0), "wrist_r": (0, -24 * s, 0)}


def bat_idle(t):
    v = _bat_flap(t, 30.0, 1.0)
    v.update({"_z": 0.05 * sn(t), "body": X(3 * sn(t, 0.2)), "head": X(-4 * sn(t, 0.3))})
    return v


def bat_walk(t):
    v = _bat_flap(t, 50.0, 2.0)
    v.update({"_z": 0.09 * sn(t * 2), "body": X(8), "head": X(-8)})
    return v


def bat_attack(t):
    i, k = phases(t, 0.38, 0.58, 1.0)
    up = {"_z": 0.20, "body": X(-16), "head": X(-18), "wing_l": (0, -70, 0), "wing_r": (0, 70, 0)}
    dive = {"_z": -0.10, "_y": -0.30, "body": X(26), "head": X(16), "wing_l": (0, -14, 0), "wing_r": (0, 14, 0)}
    return [mix(_bat_flap(t), up, k), mix(up, dive, k), mix(dive, _bat_flap(t), k), _bat_flap(t)][i]


def bat_hurt(t):
    k = math.sin(t * math.pi)
    v = _bat_flap(t, 60.0, 3.0)
    v.update({"_y": 0.12 * k, "body": X(-20 * k), "_roll": 16 * k})
    return v


def bat_death(t):
    # It drops out of the air and lies where it lands, wings half spread. It
    # used to roll onto its side on the way down, which is a roll about its own
    # spine: facing the camera that turned its wings flat, but side on it stood
    # them straight up the screen, and the bat hung in the air a full wingspan
    # tall and never seemed to land at all.
    k = ease(t)
    return {"_z": -0.58 * k, "body": X(20 * k),
            "wing_l": (0, -35 * k, 0), "wing_r": (0, 35 * k, 0)}


def build_hound():
    """Long, low and thin: a dog that has been down here too long."""
    r = Rig()
    r.joint("body", (0, 0, 0.44), rest=(-4, 0, 0))
    r.add("chest", E(0.14, 0.17, 0.14), "hound", "body", loc=(0, -0.14, 0.02))
    r.add("belly", E(0.12, 0.20, 0.11), "hound_dk", "body", loc=(0, 0.04, -0.03))
    r.add("hips", E(0.13, 0.13, 0.13), "hound", "body", loc=(0, 0.22, 0.02))
    for k in range(4):
        r.limb("spine", (0, -0.14 + k * 0.12, 0.13), (0, -0.16 + k * 0.12, 0.19), 0.022, "hound_dk", "body",
               r_tip=0.004)
    for sx, side in ((-1, "l"), (1, "r")):
        for tag, y in (("fore", -0.16), ("hind", 0.20)):
            j = tag + "_" + side
            r.joint(j, (sx * 0.10, y, -0.06), "body", rest=(8 if tag == "hind" else 4, 0, 0))
            r.limb("thigh", (0, 0, 0), (0, 0.02 if tag == "hind" else -0.02, -0.18), 0.045, "hound", j,
                   r_tip=0.032)
            r.joint(j + "_knee", (0, 0.02 if tag == "hind" else -0.02, -0.18), j, rest=(-18, 0, 0))
            r.limb("shin", (0, 0, 0), (0, 0, -0.18), 0.03, "hound_dk", j + "_knee", r_tip=0.022)
            r.add("paw", E(0.04, 0.06, 0.03), "hound_dk", j + "_knee", loc=(0, -0.02, -0.19))
    r.joint("neck", (0, -0.26, 0.06), "body", rest=(14, 0, 0))
    r.limb("neckp", (0, 0, 0), (0, -0.10, 0.04), 0.06, "hound", "neck", r_tip=0.05)
    r.joint("head", (0, -0.10, 0.04), "neck", rest=(-10, 0, 0))
    r.add("skull", E(0.075, 0.09, 0.075), "hound", "head")
    r.add("muzzle", E(0.05, 0.10, 0.045), "hound_dk", "head", loc=(0, -0.13, -0.02))
    r.add("jaw", E(0.042, 0.085, 0.025), "hound", "head", loc=(0, -0.12, -0.06))
    for sx in (-1, 1):
        r.add("ear", E(0.02, 0.03, 0.06), "hound_dk", "head", loc=(sx * 0.055, 0.03, 0.08),
              rot=(0, sx * -0.25, 0))
        r.add("eye", E(0.022, 0.018, 0.02), "hound_eye_glow", "head", loc=(sx * 0.045, -0.07, 0.03))
        for k in range(3):
            r.limb("tooth", (sx * 0.028, -0.13 - k * 0.03, -0.035), (sx * 0.028, -0.13 - k * 0.03, -0.06),
                   0.009, "ankou_bone", "head", r_tip=0.002)
    r.joint("tail1", (0, 0.32, 0.04), "body", rest=(52, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.20), 0.028, "hound", "tail1", r_tip=0.018)
    r.joint("tail2", (0, 0, -0.20), "tail1", rest=(14, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.18), 0.018, "hound_dk", "tail2", r_tip=0.006)
    r.pose.scale = (1.55, 1.55, 1.55)
    return r


def hound_idle(t):
    s = sn(t)
    return {"_z": 0.012 * s, "body": X(2 * s), "head": (0, 0, 5 * sn(t, 0.3)),
            "tail1": (0, 0, 14 * s), "tail2": (0, 0, 18 * sn(t, 0.25))}


def hound_walk(t):
    s = sn(t)
    return {"fore_l": fwd(24 * s), "hind_r": fwd(22 * s), "fore_r": fwd(-24 * s), "hind_l": fwd(-22 * s),
            "fore_l_knee": X(18 * max(0.0, -s)), "fore_r_knee": X(18 * max(0.0, s)),
            "hind_l_knee": X(20 * max(0.0, s)), "hind_r_knee": X(20 * max(0.0, -s)),
            "_z": 0.03 * abs(s), "body": (0, 0, 4 * s), "neck": X(6), "head": (0, 0, -4 * s),
            "tail1": (0, 0, 22 * s)}


def hound_attack(t):
    i, k = phases(t, 0.34, 0.54, 1.0)
    coil = {"body": X(-14), "_y": 0.10, "neck": X(-20), "head": X(-8),
            "hind_l": fwd(-26), "hind_r": fwd(-24), "hind_l_knee": X(34), "hind_r_knee": X(34)}
    lunge = {"body": X(16), "_y": -0.34, "_z": 0.10, "neck": X(26), "head": X(18),
             "fore_l": fwd(40), "fore_r": fwd(34), "tail1": (0, 0, -20)}
    return [mix({}, coil, k), mix(coil, lunge, k), mix(lunge, {}, k), {}][i]


def hound_hurt(t):
    k = math.sin(t * math.pi)
    return {"body": X(-14 * k), "_y": 0.12 * k, "neck": X(-22 * k), "head": (0, 0, 16 * k),
            "tail1": (0, 0, -24 * k)}


def hound_death(t):
    k = ease(t * 1.1)
    return {"_roll": 78 * k, "_z": -0.26 * k, "neck": X(30 * k), "head": X(24 * k),
            "fore_l": fwd(36 * k), "hind_l": fwd(30 * k), "tail1": (0, 0, 26 * k)}


def build_wolf():
    """A hound's frame with a living animal's coat: a ruff at the neck, a pale
    belly and muzzle, pricked ears and a brush of a tail carried low. Joints
    are the hound's, so it moves the way the hound does."""
    r = Rig()
    r.joint("body", (0, 0, 0.44), rest=(-2, 0, 0))
    r.add("chest", E(0.145, 0.18, 0.15), "wolf_fur", "body", loc=(0, -0.14, 0.02))
    r.add("belly", E(0.115, 0.20, 0.105), "wolf_belly", "body", loc=(0, 0.04, -0.04))
    r.add("hips", E(0.125, 0.13, 0.125), "wolf_fur", "body", loc=(0, 0.22, 0.02))
    r.add("saddle", E(0.10, 0.26, 0.06), "wolf_fur_dk", "body", loc=(0, 0.02, 0.11))
    for sx, side in ((-1, "l"), (1, "r")):
        for tag, y in (("fore", -0.16), ("hind", 0.20)):
            j = tag + "_" + side
            r.joint(j, (sx * 0.10, y, -0.06), "body", rest=(8 if tag == "hind" else 4, 0, 0))
            r.limb("thigh", (0, 0, 0), (0, 0.02 if tag == "hind" else -0.02, -0.18), 0.046, "wolf_fur", j,
                   r_tip=0.032)
            r.joint(j + "_knee", (0, 0.02 if tag == "hind" else -0.02, -0.18), j, rest=(-18, 0, 0))
            r.limb("shin", (0, 0, 0), (0, 0, -0.18), 0.030, "wolf_fur_dk", j + "_knee", r_tip=0.022)
            r.add("paw", E(0.04, 0.06, 0.03), "wolf_fur_dk", j + "_knee", loc=(0, -0.02, -0.19))
    r.joint("neck", (0, -0.26, 0.06), "body", rest=(14, 0, 0))
    r.limb("neckp", (0, 0, 0), (0, -0.10, 0.04), 0.068, "wolf_fur", "neck", r_tip=0.055)
    r.add("ruff", E(0.095, 0.075, 0.095), "wolf_belly", "neck", loc=(0, -0.03, -0.035))
    r.joint("head", (0, -0.10, 0.04), "neck", rest=(-10, 0, 0))
    r.add("skull", E(0.080, 0.092, 0.078), "wolf_fur", "head")
    r.add("muzzle", E(0.046, 0.105, 0.042), "wolf_belly", "head", loc=(0, -0.13, -0.022))
    r.add("nose", E(0.022, 0.020, 0.020), "wolf_nose", "head", loc=(0, -0.235, -0.012))
    for sx in (-1, 1):
        r.limb("ear", (sx * 0.050, 0.025, 0.055), (sx * 0.066, 0.040, 0.150), 0.030, "wolf_fur_dk", "head",
               r_tip=0.006)
        r.add("eye", E(0.020, 0.016, 0.018), "beast_eye", "head", loc=(sx * 0.046, -0.072, 0.030))
    r.joint("tail1", (0, 0.32, 0.04), "body", rest=(34, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.20), 0.040, "wolf_fur", "tail1", r_tip=0.046)
    r.joint("tail2", (0, 0, -0.20), "tail1", rest=(10, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.17), 0.044, "wolf_fur_dk", "tail2", r_tip=0.010)
    r.pose.scale = (1.5, 1.5, 1.5)
    return r


def build_bear():
    """A boulder on four posts: more shoulder than the boar and twice the
    size, a hump over the forelegs, a broad head carried low, round ears and a
    pale muzzle. What it does is stand up."""
    r = Rig()
    _quadruped(r, "bear_fur", "bear_fur_dk", 0.46,
               chest=(0.245, 0.27, 0.235), hips=(0.235, 0.25, 0.225),
               leg_len=0.34, leg_r=0.086, fore_y=-0.16, hind_y=0.22, hip_x=0.145)
    r.add("hump", E(0.20, 0.17, 0.13), "bear_fur", "body", loc=(0, -0.13, 0.17))
    r.add("rump", E(0.20, 0.16, 0.15), "bear_fur_dk", "body", loc=(0, 0.26, 0.06))
    for sx, side in ((-1, "l"), (1, "r")):
        # Claws on the forepaws: the one detail worth pixels.
        for k in (-1, 0, 1):
            r.limb("claw", (sx * 0.0 + k * 0.030, -0.085, -0.335), (k * 0.034, -0.150, -0.350),
                   0.012, "bear_claw", "fore_" + side + "_knee", r_tip=0.003)
    r.joint("neck", (0, -0.30, 0.08), "body", rest=(18, 0, 0))
    r.limb("neckp", (0, 0, 0), (0, -0.10, 0.01), 0.125, "bear_fur", "neck", r_tip=0.105)
    r.joint("head", (0, -0.12, 0.0), "neck", rest=(-14, 0, 0))
    r.add("skull", E(0.135, 0.130, 0.120), "bear_fur", "head")
    r.add("muzzle", E(0.070, 0.105, 0.062), "bear_muzzle", "head", loc=(0, -0.145, -0.035))
    r.add("nose", E(0.034, 0.026, 0.028), "wolf_nose", "head", loc=(0, -0.245, -0.020))
    for sx in (-1, 1):
        r.add("ear", E(0.044, 0.026, 0.044), "bear_fur_dk", "head", loc=(sx * 0.100, 0.035, 0.105))
        r.add("eye", E(0.020, 0.016, 0.020), "beast_eye", "head", loc=(sx * 0.066, -0.098, 0.040))
    r.joint("tail1", (0, 0.40, 0.10), "body", rest=(30, 0, 0))
    r.add("tail", E(0.040, 0.040, 0.040), "bear_fur_dk", "tail1")
    r.pose.scale = (1.72, 1.72, 1.72)
    return r


def bear_idle(t):
    s = sn(t)
    return {"_z": 0.012 * s, "body": X(1.5 * s), "neck": X(4 * sn(t, 0.3)),
            "head": (0, 0, 6 * sn(t, 0.2))}


def bear_walk(t):
    # A lumber: the whole barrel rolls side to side over the stepping foot.
    s = sn(t)
    return {"fore_l": fwd(22 * s), "hind_r": fwd(20 * s), "fore_r": fwd(-22 * s), "hind_l": fwd(-20 * s),
            "fore_l_knee": X(16 * max(0.0, -s)), "fore_r_knee": X(16 * max(0.0, s)),
            "hind_l_knee": X(18 * max(0.0, s)), "hind_r_knee": X(18 * max(0.0, -s)),
            "_z": 0.026 * abs(s), "body": (0, 5 * s, 4 * s), "neck": X(6), "head": (0, 0, -5 * s)}


def bear_attack(t):
    # Up on its hind legs, both forepaws high, and then all of it comes down.
    i, k = phases(t, 0.40, 0.58, 1.0)
    rear = {"body": X(-46), "_z": 0.26, "_y": 0.10, "neck": X(30), "head": X(16),
            "fore_l": fwd(74), "fore_r": fwd(62), "fore_l_knee": X(-30), "fore_r_knee": X(-24),
            "hind_l": fwd(-40), "hind_r": fwd(-38), "hind_l_knee": X(30), "hind_r_knee": X(30)}
    maul = {"body": X(18), "_y": -0.34, "_z": 0.02, "neck": X(24), "head": X(22),
            "fore_l": fwd(-28), "fore_r": fwd(-34), "hind_l": fwd(18), "hind_r": fwd(16)}
    return [mix({}, rear, k), mix(rear, maul, k), mix(maul, {}, k), {}][i]


def bear_hurt(t):
    k = math.sin(t * math.pi)
    return {"body": X(-10 * k), "_y": 0.10 * k, "neck": X(-18 * k), "head": (0, 0, 16 * k)}


def bear_death(t):
    k = ease(t * 1.1)
    return {"_roll_to_camera": True, "_roll": 74 * k, "_z": -0.26 * k, "neck": X(26 * k), "head": X(20 * k),
            "fore_l": fwd(32 * k), "hind_l": fwd(28 * k)}


def build_ankou():
    """The grave's own coachman: a tall black robe, a skull in the hood, and a
    scythe it does not need to swing hard."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.86))
    r.add("skirt", C(0.22, 0.05, 0.62), "ankou_robe", "pelvis", loc=(0, 0.02, -0.26))
    for k in range(4):
        r.add("tatter", E(0.06, 0.05, 0.11), "ankou_robe_lt", "pelvis",
              loc=(-0.15 + k * 0.10, -0.02, -0.56 - (k % 2) * 0.04))
    r.joint("chest", (0, 0, 0.12), "pelvis", rest=(4, 0, 0))
    r.add("robe", E(0.21, 0.16, 0.26), "ankou_robe", "chest", loc=(0, 0, 0.18))
    r.add("mantle", E(0.25, 0.19, 0.09), "ankou_robe_lt", "chest", loc=(0, 0.01, 0.34))
    for sx in (-1, 1):
        r.add("clavicle", E(0.05, 0.05, 0.05), "ankou_bone", "chest", loc=(sx * 0.13, -0.10, 0.32))
        r.joint("shoulder_" + ("l" if sx < 0 else "r"), (sx * 0.20, 0, 0.34), "chest", rest=(0, sx * -16, 0))
        side = "l" if sx < 0 else "r"
        r.limb("sleeve", (0, 0, 0), (0, 0, -0.26), 0.07, "ankou_robe", "shoulder_" + side, r_tip=0.05)
        r.joint("elbow_" + side, (0, 0, -0.26), "shoulder_" + side, rest=(-24, 0, 0))
        r.limb("forearm", (0, 0, 0), (0, 0, -0.20), 0.035, "ankou_bone", "elbow_" + side, r_tip=0.028)
        r.joint("hand_" + side, (0, 0, -0.20), "elbow_" + side)
        r.add("palm", E(0.04, 0.035, 0.045), "ankou_bone", "hand_" + side)
        for k in (-1, 0, 1):
            r.limb("finger", (k * 0.025, -0.02, -0.03), (k * 0.04, -0.06, -0.07), 0.011, "ankou_bone",
                   "hand_" + side, r_tip=0.003)
    r.joint("neck", (0, 0, 0.48), "chest")
    r.joint("head", (0, -0.02, 0.08), "neck", rest=(-8, 0, 0))
    r.add("hood", E(0.13, 0.14, 0.15), "ankou_robe", "head", loc=(0, 0.02, 0.07))
    r.add("hood_peak", E(0.07, 0.09, 0.09), "ankou_robe_lt", "head", loc=(0, 0.07, 0.17))
    r.add("skull", E(0.085, 0.09, 0.09), "ankou_bone", "head", loc=(0, -0.06, 0.05))
    r.add("jaw", E(0.06, 0.06, 0.03), "ankou_bone", "head", loc=(0, -0.08, -0.03))
    for sx in (-1, 1):
        r.add("socket", E(0.028, 0.02, 0.028), "ankou_robe", "head", loc=(sx * 0.038, -0.12, 0.07))
        r.add("light", E(0.016, 0.012, 0.016), "ankou_eye_glow", "head", loc=(sx * 0.038, -0.13, 0.07))
    # The scythe: a long haft in the right hand and a blade off the top of it.
    r.limb("haft", (0, 0, 0.58), (0, 0, -0.62), 0.024, "spear", "hand_r")
    r.limb("blade", (0, 0.02, 0.58), (0, -0.46, 0.50), 0.035, "scythe_steel", "hand_r", r_tip=0.006)
    r.add("edge", E(0.012, 0.18, 0.05), "ankou_bone", "hand_r", loc=(0, -0.22, 0.50), rot=(0.2, 0, 0))
    r.pose.scale = (1.45, 1.45, 1.45)
    return r


def ankou_idle(t):
    s = sn(t)
    return {"_z": 0.02 * s, "chest": X(2 * s), "head": (0, 0, 4 * sn(t, 0.3)),
            "shoulder_r": fwd(8), "elbow_r": X(-30), "shoulder_l": (0, -8 - 4 * s, 0)}


def ankou_walk(t):
    s = sn(t)
    # It does not walk so much as advance: the robe sways, the feet never show.
    return {"_z": 0.04 * s, "_y": 0.01 * sn(t, 0.3), "chest": X(4 * s), "pelvis": (0, 0, 5 * s),
            "shoulder_r": fwd(8), "elbow_r": X(-30), "shoulder_l": (0, -12 - 8 * s, 0),
            "head": (0, 0, 6 * sn(t, 0.2))}


def ankou_attack(t):
    i, k = phases(t, 0.40, 0.60, 1.0)
    rest = {"shoulder_r": fwd(8), "elbow_r": X(-30)}
    raise_ = {"shoulder_r": fwd(-60), "elbow_r": X(-70), "hand_r": X(30), "chest": (-8, 0, -20),
              "_z": 0.10}
    reap = {"shoulder_r": fwd(52), "elbow_r": X(-8), "hand_r": X(-20), "chest": (14, 0, 26),
            "_y": -0.22, "head": X(10)}
    return [mix(rest, raise_, k), mix(raise_, reap, k), mix(reap, rest, k), rest][i]


def ankou_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-16 * k), "_y": 0.12 * k, "head": X(-14 * k),
            "shoulder_l": (0, -40 * k, 0), "shoulder_r": fwd(8)}


def ankou_death(t):
    k = ease(t)
    return {"_z": -0.44 * k, "chest": X(34 * k), "neck": X(24 * k), "head": X(20 * k),
            "shoulder_l": fwd(-26 * k), "shoulder_r": fwd(-20 * k), "_roll": 10 * k}


def build_banshee():
    """A woman-shaped cold spot: hair and a gown and a mouth open on a note you
    cannot hear. Nothing below the waist but a trail."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.92))
    r.add("gown", C(0.20, 0.03, 0.66), "banshee", "pelvis", loc=(0, 0.02, -0.30))
    for k in range(4):
        r.add("wisp", E(0.05 - k * 0.006, 0.04, 0.12), "banshee_dk", "pelvis",
              loc=(-0.13 + k * 0.09, 0.01, -0.62 - k * 0.05))
    r.add("trail", E(0.07, 0.06, 0.12), "banshee_glow", "pelvis", loc=(0, 0.02, -0.80))
    r.joint("chest", (0, 0, 0.14), "pelvis", rest=(-4, 0, 0))
    r.add("torso", E(0.15, 0.12, 0.22), "banshee", "chest", loc=(0, 0, 0.14))
    r.add("collar", E(0.18, 0.13, 0.06), "banshee_dk", "chest", loc=(0, 0.01, 0.26))
    for sx in (-1, 1):
        side = "l" if sx < 0 else "r"
        r.joint("shoulder_" + side, (sx * 0.15, 0, 0.26), "chest", rest=(0, sx * -30, 0))
        r.limb("arm", (0, 0, 0), (0, 0, -0.24), 0.045, "banshee", "shoulder_" + side, r_tip=0.03)
        r.joint("elbow_" + side, (0, 0, -0.24), "shoulder_" + side, rest=(-20, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.20), 0.03, "banshee_dk", "elbow_" + side, r_tip=0.02)
        r.joint("hand_" + side, (0, 0, -0.20), "elbow_" + side)
        for k in (-1, 0, 1):
            r.limb("finger", (k * 0.02, -0.01, 0), (k * 0.045, -0.06, -0.08), 0.01, "banshee_glow",
                   "hand_" + side, r_tip=0.003)
    r.joint("neck", (0, 0, 0.40), "chest")
    r.joint("head", (0, -0.01, 0.07), "neck", rest=(-6, 0, 0))
    r.add("skull", E(0.085, 0.09, 0.095), "banshee", "head", loc=(0, 0, 0.04))
    r.add("jawdrop", E(0.045, 0.05, 0.055), "banshee_dk", "head", loc=(0, -0.06, -0.06))
    r.add("mouth", E(0.03, 0.03, 0.04), "shroud_dk", "head", loc=(0, -0.08, -0.04))
    for sx in (-1, 1):
        r.add("eye", E(0.022, 0.016, 0.03), "banshee_glow", "head", loc=(sx * 0.038, -0.075, 0.06))
        # Hair, streaming back and up as though she is always falling.
        for k in range(3):
            r.limb("hair", (sx * (0.05 + k * 0.02), 0.02, 0.10 - k * 0.03),
                   (sx * (0.10 + k * 0.05), 0.24 + k * 0.06, 0.20 - k * 0.10), 0.028, "banshee_hair",
                   "head", r_tip=0.006)
    r.limb("hair", (0, 0.04, 0.12), (0, 0.30, 0.16), 0.035, "banshee_hair", "head", r_tip=0.008)
    r.pose.scale = (1.4, 1.4, 1.4)
    return r


def banshee_idle(t):
    s = sn(t)
    return {"_z": 0.06 * s, "chest": X(3 * s), "head": (0, 0, 6 * sn(t, 0.3)),
            "shoulder_l": (0, -26 - 6 * s, 0), "shoulder_r": (0, 26 + 6 * s, 0),
            "pelvis": (0, 0, 5 * sn(t, 0.2))}


def banshee_walk(t):
    s = sn(t)
    return {"_z": 0.09 * s, "_y": 0.02 * sn(t, 0.25), "chest": X(5 * s),
            "shoulder_l": (0, -34 - 10 * s, 0), "shoulder_r": (0, 34 + 10 * s, 0),
            "head": (0, 0, 10 * sn(t, 0.2)), "pelvis": (0, 0, 8 * s)}


def banshee_attack(t):
    """The scream: she draws up, throws her head back, and everything in front
    of her gets it."""
    i, k = phases(t, 0.42, 0.62, 1.0)
    draw = {"_z": 0.20, "chest": X(-16), "neck": X(-14), "head": X(-20),
            "shoulder_l": fwd(-40), "shoulder_r": fwd(-40), "elbow_l": X(-60), "elbow_r": X(-60)}
    scream = {"_z": 0.06, "_y": -0.20, "chest": X(18), "neck": X(16), "head": X(22),
              "shoulder_l": (0, -80, 0), "shoulder_r": (0, 80, 0), "elbow_l": X(-6), "elbow_r": X(-6)}
    return [mix({}, draw, k), mix(draw, scream, k), mix(scream, {}, k), {}][i]


def banshee_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-20 * k), "_y": 0.14 * k, "_z": 0.06 * k, "head": X(-16 * k),
            "shoulder_l": (0, -50 * k, 0), "shoulder_r": (0, 50 * k, 0)}


def banshee_death(t):
    k = ease(t)
    return {"_z": -0.50 * k, "chest": X(30 * k), "head": X(30 * k), "_roll": 14 * k,
            "shoulder_l": fwd(-34 * k), "shoulder_r": fwd(-34 * k)}



# =================================================================================
#  The woodland animals
#
#  Four quadrupeds off one plan -- a barrel body on four hinged legs, a neck and
#  a head -- told apart by proportion rather than detail, because at thirty-odd
#  pixels proportion is all that survives: the boar is a wedge with no daylight
#  under it, the deer is mostly leg, the fox is a low line with a tail as long
#  as it is, and the hare is a crouch with two ears.
# =================================================================================
def _quadruped(r, colour, dark, body_z, chest, hips, leg_len, leg_r, fore_y, hind_y, hip_x):
    """The shared frame: chest, belly and hips, and four two-bone legs."""
    r.joint("body", (0, 0, body_z), rest=(-2, 0, 0))
    r.add("chest", E(chest[0], chest[1], chest[2]), colour, "body", loc=(0, -0.12, 0.02))
    r.add("hips", E(hips[0], hips[1], hips[2]), colour, "body", loc=(0, 0.18, 0.01))
    r.add("belly", E(chest[0] * 0.86, 0.22, chest[2] * 0.70), dark, "body", loc=(0, 0.02, -0.05))
    for sx, side in ((-1, "l"), (1, "r")):
        for tag, y in (("fore", fore_y), ("hind", hind_y)):
            j = tag + "_" + side
            r.joint(j, (sx * hip_x, y, -0.04), "body", rest=(6 if tag == "hind" else 3, 0, 0))
            r.limb("thigh", (0, 0, 0), (0, 0.01 if tag == "hind" else -0.01, -leg_len * 0.55),
                   leg_r, colour, j, r_tip=leg_r * 0.72)
            r.joint(j + "_knee", (0, 0.01 if tag == "hind" else -0.01, -leg_len * 0.55), j,
                    rest=(-14, 0, 0))
            r.limb("shin", (0, 0, 0), (0, 0, -leg_len * 0.45), leg_r * 0.66, dark, j + "_knee",
                   r_tip=leg_r * 0.5)
            r.add("foot", E(leg_r * 0.9, leg_r * 1.2, leg_r * 0.6), dark, j + "_knee",
                  loc=(0, -0.012, -leg_len * 0.47))


def build_boar():
    """All shoulder and no neck: a wedge that reads as bad news from behind."""
    r = Rig()
    _quadruped(r, "boar_hide", "boar_hide_dk", 0.40,
               chest=(0.20, 0.24, 0.20), hips=(0.17, 0.18, 0.17),
               leg_len=0.30, leg_r=0.055, fore_y=-0.14, hind_y=0.19, hip_x=0.11)
    # The hump over the shoulders, and the bristles along it.
    r.add("hump", E(0.17, 0.15, 0.13), "boar_hide", "body", loc=(0, -0.10, 0.14))
    for k in range(6):
        r.limb("bristle", (0, -0.20 + k * 0.09, 0.17), (0, -0.26 + k * 0.09, 0.36),
               0.024, "boar_bristle", "body", r_tip=0.004)
    r.joint("neck", (0, -0.24, 0.06), "body", rest=(16, 0, 0))
    r.joint("head", (0, -0.10, -0.01), "neck", rest=(-12, 0, 0))
    r.add("skull", E(0.10, 0.12, 0.10), "boar_hide", "head")
    r.add("snout", E(0.058, 0.16, 0.050), "boar_hide_dk", "head", loc=(0, -0.19, -0.05))
    r.add("nose", E(0.052, 0.035, 0.042), "boar_snout", "head", loc=(0, -0.32, -0.05))
    for sx in (-1, 1):
        r.add("ear", E(0.028, 0.02, 0.06), "boar_hide_dk", "head", loc=(sx * 0.075, 0.02, 0.10),
              rot=(0, sx * -0.3, 0))
        r.add("eye", E(0.022, 0.02, 0.022), "beast_eye", "head", loc=(sx * 0.062, -0.09, 0.04))
        # Tusks, curling up out of the lower jaw: the one detail worth pixels.
        r.limb("tusk", (sx * 0.055, -0.24, -0.085), (sx * 0.080, -0.33, 0.055),
               0.022, "boar_tusk", "head", r_tip=0.005)
    r.joint("tail1", (0, 0.26, 0.10), "body", rest=(24, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.10), 0.016, "boar_hide_dk", "tail1", r_tip=0.010)
    r.pose.scale = (1.5, 1.5, 1.5)
    return r


def boar_idle(t):
    s = sn(t)
    return {"_z": 0.010 * s, "body": X(2 * s), "neck": X(3 * sn(t, 0.3)),
            "head": (0, 0, 4 * sn(t, 0.2)), "tail1": (0, 0, 16 * s)}


def boar_walk(t):
    s = sn(t)
    return {"fore_l": fwd(20 * s), "hind_r": fwd(18 * s), "fore_r": fwd(-20 * s), "hind_l": fwd(-18 * s),
            "fore_l_knee": X(14 * max(0.0, -s)), "fore_r_knee": X(14 * max(0.0, s)),
            "hind_l_knee": X(16 * max(0.0, s)), "hind_r_knee": X(16 * max(0.0, -s)),
            "_z": 0.022 * abs(s), "body": (0, 0, 3 * s), "head": (0, 0, -3 * s),
            "tail1": (0, 0, 20 * s)}


def boar_attack(t):
    # A charge: back on the haunches, then everything behind a lowered head.
    i, k = phases(t, 0.32, 0.52, 1.0)
    coil = {"body": X(-12), "_y": 0.10, "neck": X(-14), "head": X(-10),
            "hind_l": fwd(-22), "hind_r": fwd(-20), "hind_l_knee": X(28), "hind_r_knee": X(28)}
    gore = {"body": X(14), "_y": -0.30, "_z": 0.05, "neck": X(22), "head": X(26),
            "fore_l": fwd(34), "fore_r": fwd(30), "tail1": (0, 0, -18)}
    return [mix({}, coil, k), mix(coil, gore, k), mix(gore, {}, k), {}][i]


def boar_hurt(t):
    k = math.sin(t * math.pi)
    return {"body": X(-12 * k), "_y": 0.10 * k, "neck": X(-16 * k), "head": (0, 0, 14 * k)}


def boar_death(t):
    k = ease(t * 1.1)
    return {"_roll_to_camera": True, "_roll": 76 * k, "_z": -0.22 * k, "neck": X(24 * k), "head": X(18 * k),
            "fore_l": fwd(30 * k), "hind_l": fwd(26 * k)}


def build_deer():
    """A red deer stag: a deep barrel carried level on slim legs, a maned neck
    rising *forward* out of the chest, a long head with big ears, and a rack
    that spreads wider than the shoulders.

    This is the third deer. The first came with the art the project started
    with, a spotted fawn in another style. The second was mostly leg -- a body
    the size of a shoebox on stilts three times as long -- and its neck leaned
    the wrong way: a limb pointing up leans *forward* under a positive turn
    about X, and it was given a negative one, so the neck went back over the
    shoulders and the head sat on top of the body looking at the sky. From in
    front that is a brown pillar; from the side, a table with a stick on it.

    What reads as "deer" at this size, in the order it matters: the head out
    in front of the chest and above it, with daylight under the jaw; ears that
    stick out sideways; antlers wide enough to break the silhouette from the
    front as well as the side; a body longer than it is tall; legs no longer
    than the body is deep plus a bit, dark toward the hoof; and the pale rump
    that is the last thing anyone sees of one."""
    r = Rig()
    _quadruped(r, "deer_hide", "deer_hide_dk", 0.66,
               chest=(0.150, 0.215, 0.172), hips=(0.138, 0.188, 0.158),
               leg_len=0.60, leg_r=0.045, fore_y=-0.17, hind_y=0.20, hip_x=0.094)
    # Pale underneath, over the frame's dark belly, and the back a shade darker.
    r.add("underbelly", E(0.118, 0.215, 0.080), "deer_belly", "body", loc=(0, 0.03, -0.105))
    r.add("saddle", E(0.105, 0.30, 0.060), "deer_hide_dk", "body", loc=(0, 0.04, 0.135))
    # Shoulder and haunch, so the legs taper out of a body instead of being
    # pegged into a box.
    for sx, side in ((-1, "l"), (1, "r")):
        r.add("shoulder", E(0.070, 0.095, 0.125), "deer_hide", "fore_" + side, loc=(sx * 0.010, 0.0, -0.030))
        r.add("haunch", E(0.082, 0.120, 0.145), "deer_hide", "hind_" + side, loc=(sx * 0.008, 0.010, -0.035))
        for tag in ("fore", "hind"):
            r.add("hoof", E(0.040, 0.050, 0.032), "deer_hoof", tag + "_" + side + "_knee",
                  loc=(0, -0.014, -0.60 * 0.47))
    # The rump patch and the tail over it.
    r.add("rump", E(0.112, 0.055, 0.115), "deer_belly", "body", loc=(0, 0.345, 0.025))
    r.joint("tail1", (0, 0.375, 0.105), "body", rest=(28, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.10), 0.026, "deer_hide_dk", "tail1", r_tip=0.014)

    # The neck: up out of the front of the chest and leaning forward as it
    # goes, thick at the base where the mane is.
    r.joint("neck", (0, -0.265, 0.085), "body", rest=(30, 0, 0))
    r.limb("neckp", (0, 0, 0), (0, 0, 0.345), 0.082, "deer_hide", "neck", r_tip=0.056)
    r.add("mane", E(0.092, 0.085, 0.165), "deer_hide_dk", "neck", loc=(0, 0.012, 0.115))
    r.add("throat", E(0.046, 0.040, 0.130), "deer_belly", "neck", loc=(0, -0.058, 0.215))

    # The head, carried level with the nose a little down.
    r.joint("head", (0, 0, 0.345), "neck", rest=(-20, 0, 0))
    r.add("skull", E(0.080, 0.098, 0.080), "deer_hide", "head", loc=(0, -0.025, 0.020))
    r.add("muzzle", E(0.047, 0.110, 0.045), "deer_hide", "head", loc=(0, -0.160, -0.012))
    r.add("chin", E(0.036, 0.070, 0.024), "deer_belly", "head", loc=(0, -0.165, -0.046))
    r.add("nose", E(0.032, 0.026, 0.028), "deer_hoof", "head", loc=(0, -0.262, -0.008))
    for sx in (-1, 1):
        # Ears out to the side and a little back: from the front they are the
        # widest thing on the head after the rack.
        r.limb("ear", (sx * 0.062, 0.030, 0.060), (sx * 0.200, 0.060, 0.120), 0.042, "deer_hide_dk", "head",
               r_tip=0.014)
        # Pale inside, facing forward, or from in front they are lost in the mane.
        r.limb("ear_in", (sx * 0.085, 0.008, 0.070), (sx * 0.178, 0.030, 0.112), 0.024, "deer_belly", "head",
               r_tip=0.008)
        r.add("eye", E(0.020, 0.018, 0.020), "beast_eye", "head", loc=(sx * 0.064, -0.078, 0.040))
        # The rack: a beam up and out, a second length up and in over it, and
        # three tines -- brow, tray and crown -- thick enough to be a pixel.
        r.limb("beam", (sx * 0.036, 0.040, 0.085), (sx * 0.225, 0.110, 0.290), 0.027, "deer_antler", "head",
               r_tip=0.021)
        r.limb("beam2", (sx * 0.225, 0.110, 0.290), (sx * 0.275, 0.050, 0.500), 0.021, "deer_antler", "head",
               r_tip=0.010)
        r.limb("brow", (sx * 0.060, 0.040, 0.120), (sx * 0.135, -0.085, 0.225), 0.016, "deer_antler", "head",
               r_tip=0.006)
        r.limb("tray", (sx * 0.160, 0.085, 0.225), (sx * 0.305, 0.010, 0.300), 0.015, "deer_antler", "head",
               r_tip=0.006)
        r.limb("crown", (sx * 0.255, 0.085, 0.400), (sx * 0.365, 0.105, 0.470), 0.014, "deer_antler", "head",
               r_tip=0.005)
    r.pose.scale = (1.22, 1.22, 1.22)
    return r


def deer_idle(t):
    # Head up, ears working, a flick of the tail: something listening.
    s = sn(t)
    return {"_z": 0.010 * s, "neck": X(-3 * s), "head": (2 * s, 0, 9 * sn(t, 0.28)),
            "tail1": (0, 0, 20 * sn(t, 0.4))}


def deer_walk(t):
    s = sn(t)
    return {"fore_l": fwd(26 * s), "hind_r": fwd(24 * s), "fore_r": fwd(-26 * s), "hind_l": fwd(-24 * s),
            "fore_l_knee": X(20 * max(0.0, -s)), "fore_r_knee": X(20 * max(0.0, s)),
            "hind_l_knee": X(22 * max(0.0, s)), "hind_r_knee": X(22 * max(0.0, -s)),
            "_z": 0.03 * abs(s), "neck": X(6 + 4 * s), "head": (-4, 0, -4 * s), "tail1": (0, 0, 16 * s)}


def deer_attack(t):
    # It has no attack in the game; this is the warning stamp it gives instead.
    i, k = phases(t, 0.35, 0.6, 1.0)
    rear = {"_z": 0.10, "body": X(-16), "neck": X(-12), "head": X(-8), "fore_l": fwd(-40), "fore_r": fwd(-36),
            "fore_l_knee": X(38), "fore_r_knee": X(34)}
    # And down with the rack lowered: the one thing about a stag worth backing away from.
    down = {"_z": -0.02, "body": X(8), "_y": -0.10, "neck": X(34), "head": X(26), "fore_l": fwd(18), "fore_r": fwd(16)}
    return [mix({}, rear, k), mix(rear, down, k), mix(down, {}, k), {}][i]


def deer_hurt(t):
    k = math.sin(t * math.pi)
    return {"body": X(-10 * k), "_y": 0.12 * k, "neck": X(-14 * k), "head": (0, 0, 18 * k),
            "tail1": (0, 0, -20 * k)}


def deer_death(t):
    k = ease(t)
    return {"_roll_to_camera": True, "_roll": 80 * k, "_z": -0.34 * k, "neck": X(26 * k), "head": X(16 * k),
            "fore_l": fwd(34 * k), "hind_l": fwd(30 * k), "fore_l_knee": X(30 * k)}


def build_fox():
    """A low line with a tail as long as the rest of it, and the black socks
    that say fox at any size."""
    r = Rig()
    _quadruped(r, "fox_fur", "fox_belly", 0.34,
               chest=(0.105, 0.17, 0.105), hips=(0.10, 0.13, 0.10),
               leg_len=0.26, leg_r=0.032, fore_y=-0.11, hind_y=0.15, hip_x=0.072)
    for sx, side in ((-1, "l"), (1, "r")):
        for tag in ("fore", "hind"):
            r.add("sock", E(0.036, 0.042, 0.055), "fox_sock", tag + "_" + side + "_knee",
                  loc=(0, 0, -0.10))
    r.joint("neck", (0, -0.19, 0.05), "body", rest=(12, 0, 0))
    r.limb("neckp", (0, 0, 0), (0, -0.07, 0.03), 0.048, "fox_fur", "neck", r_tip=0.042)
    r.joint("head", (0, -0.07, 0.03), "neck", rest=(-10, 0, 0))
    r.add("skull", E(0.058, 0.068, 0.056), "fox_fur", "head")
    r.add("muzzle", E(0.032, 0.085, 0.030), "fox_fur", "head", loc=(0, -0.12, -0.025))
    r.add("cheek", E(0.055, 0.035, 0.035), "fox_belly", "head", loc=(0, -0.075, -0.045))
    r.add("nose", E(0.022, 0.018, 0.02), "fox_sock", "head", loc=(0, -0.195, -0.02))
    for sx in (-1, 1):
        # Big triangular ears, dark on the outside.
        r.limb("ear", (sx * 0.042, 0.01, 0.045), (sx * 0.062, 0.02, 0.135), 0.030,
               "fox_fur_dk", "head", r_tip=0.004)
        r.add("eye", E(0.018, 0.016, 0.018), "beast_eye", "head", loc=(sx * 0.042, -0.055, 0.025))
    r.joint("tail1", (0, 0.24, 0.06), "body", rest=(64, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.20), 0.055, "fox_fur", "tail1", r_tip=0.050)
    r.joint("tail2", (0, 0, -0.20), "tail1", rest=(10, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.14), 0.048, "fox_belly", "tail2", r_tip=0.024)
    r.pose.scale = (1.45, 1.45, 1.45)
    return r


def fox_idle(t):
    s = sn(t)
    return {"_z": 0.008 * s, "head": (0, 0, 8 * sn(t, 0.3)), "neck": X(3 * s),
            "tail1": (0, 0, 20 * s), "tail2": (0, 0, 16 * sn(t, 0.2))}


def fox_walk(t):
    s = sn(t)
    return {"fore_l": fwd(28 * s), "hind_r": fwd(26 * s), "fore_r": fwd(-28 * s), "hind_l": fwd(-26 * s),
            "fore_l_knee": X(20 * max(0.0, -s)), "fore_r_knee": X(20 * max(0.0, s)),
            "hind_l_knee": X(22 * max(0.0, s)), "hind_r_knee": X(22 * max(0.0, -s)),
            "_z": 0.024 * abs(s), "body": (0, 0, 4 * s), "tail1": (0, 0, 26 * s)}


def fox_attack(t):
    i, k = phases(t, 0.34, 0.54, 1.0)
    coil = {"body": X(-14), "_y": 0.09, "neck": X(-18), "head": X(-8),
            "hind_l": fwd(-26), "hind_r": fwd(-24), "hind_l_knee": X(32), "hind_r_knee": X(32)}
    snap = {"body": X(14), "_y": -0.26, "_z": 0.08, "neck": X(24), "head": X(16),
            "fore_l": fwd(38), "fore_r": fwd(32), "tail1": (0, 0, -24)}
    return [mix({}, coil, k), mix(coil, snap, k), mix(snap, {}, k), {}][i]


def fox_hurt(t):
    k = math.sin(t * math.pi)
    return {"body": X(-12 * k), "_y": 0.10 * k, "neck": X(-18 * k), "head": (0, 0, 16 * k),
            "tail1": (0, 0, -28 * k)}


def fox_death(t):
    k = ease(t * 1.1)
    return {"_roll_to_camera": True, "_roll": 78 * k, "_z": -0.18 * k, "neck": X(26 * k), "head": X(20 * k),
            "fore_l": fwd(32 * k), "hind_l": fwd(28 * k), "tail1": (0, 0, 30 * k)}


def build_hare():
    """A crouch with two ears. The haunches are half of it, so the hop has
    somewhere to come from."""
    r = Rig()
    _quadruped(r, "hare_fur", "hare_belly", 0.26,
               chest=(0.085, 0.12, 0.085), hips=(0.11, 0.13, 0.115),
               leg_len=0.16, leg_r=0.028, fore_y=-0.08, hind_y=0.10, hip_x=0.058)
    # Hind legs folded under it rather than hanging: a hare at rest sits on them.
    r.joint("haunch_l", (-0.075, 0.09, -0.01), "body", rest=(26, 0, 0))
    r.joint("haunch_r", (0.075, 0.09, -0.01), "body", rest=(26, 0, 0))
    for j in ("haunch_l", "haunch_r"):
        r.add("haunch", E(0.055, 0.085, 0.075), "hare_fur", j)
    r.joint("neck", (0, -0.13, 0.05), "body", rest=(8, 0, 0))
    r.joint("head", (0, -0.05, 0.04), "neck", rest=(-6, 0, 0))
    r.add("skull", E(0.055, 0.065, 0.055), "hare_fur", "head")
    r.add("muzzle", E(0.032, 0.05, 0.030), "hare_fur", "head", loc=(0, -0.075, -0.025))
    r.add("nose", E(0.018, 0.015, 0.016), "hare_ear_in", "head", loc=(0, -0.115, -0.02))
    for sx in (-1, 1):
        r.limb("ear", (sx * 0.030, 0.015, 0.05), (sx * 0.058, 0.055, 0.34), 0.032,
               "hare_fur", "head", r_tip=0.018)
        r.limb("ear_in", (sx * 0.030, 0.000, 0.055), (sx * 0.055, 0.040, 0.325), 0.018,
               "hare_ear_in", "head", r_tip=0.008)
        r.add("eye", E(0.020, 0.018, 0.020), "beast_eye", "head", loc=(sx * 0.044, -0.042, 0.022))
    r.add("scut", E(0.038, 0.032, 0.038), "hare_belly", "body", loc=(0, 0.19, 0.03))
    r.pose.scale = (1.85, 1.85, 1.85)
    return r


def hare_idle(t):
    s = sn(t)
    # A hare at rest is ears and nose: the body barely moves.
    return {"_z": 0.006 * s, "head": (0, 0, 6 * sn(t, 0.25)), "neck": X(2 * s)}


def hare_walk(t):
    # It hops rather than walks: both hind legs together, a beat in the air.
    k = (t % 1.0)
    lift = max(0.0, math.sin(k * math.pi))
    return {"_z": 0.10 * lift, "_y": -0.02 * lift,
            "body": X(-14 * lift), "neck": X(10 * lift), "head": X(-8 * lift),
            "haunch_l": X(-34 * lift), "haunch_r": X(-34 * lift),
            "hind_l": fwd(30 * lift), "hind_r": fwd(30 * lift),
            "fore_l": fwd(-26 * lift), "fore_r": fwd(-26 * lift)}


def hare_attack(t):
    i, k = phases(t, 0.4, 0.7, 1.0)
    rear = {"_z": 0.06, "body": X(-24), "neck": X(-12), "fore_l": fwd(-42), "fore_r": fwd(-38)}
    kick = {"_z": 0.0, "body": X(8), "fore_l": fwd(24), "fore_r": fwd(20)}
    return [mix({}, rear, k), mix(rear, kick, k), mix(kick, {}, k), {}][i]


def hare_hurt(t):
    k = math.sin(t * math.pi)
    return {"body": X(-16 * k), "_y": 0.08 * k, "head": (0, 0, 20 * k), "neck": X(-10 * k)}


def hare_death(t):
    k = ease(t * 1.1)
    return {"_roll_to_camera": True, "_roll": 82 * k, "_z": -0.12 * k, "neck": X(22 * k), "head": X(16 * k),
            "fore_l": fwd(28 * k), "haunch_l": X(-30 * k), "haunch_r": X(-30 * k)}



# =================================================================================
#  The orcs
#
#  One build at three sizes, because the player meets them in that order and
#  should be able to tell which is in front of them before it is in reach: a
#  grunt with a club, a raider in leather with an axe and a shield, and the
#  Warchief, a head taller again, in iron with a two-hander and a red war cape.
#
#  What makes an orc read at sixty-four pixels is the silhouette -- shoulders
#  far wider than the hips, a head sunk between them with no neck showing, and
#  two tusks coming up out of the jaw -- not the green.
# =================================================================================
def _orc(bulk, height, r_leg, r_arm):
    r = Rig()
    r.joint("pelvis", (0, 0, 0.60 * height))
    r.add("hips", E(0.15 * bulk, 0.11 * bulk, 0.10), "orc_hide", "pelvis")
    humanoid_legs(r, -0.03, 0.09 * bulk, 0.24 * height, 0.24 * height, r_leg,
                  "orc_skin", "orc_skin_dk", foot_col="orc_hide_dk")
    r.joint("chest", (0, 0, 0.07), "pelvis", rest=(8, 0, 0))
    # A slab of a torso that widens upward: the shoulders are the whole point.
    r.add("torso", E(0.17 * bulk, 0.12 * bulk, 0.18 * height), "orc_skin", "chest",
          loc=(0, 0, 0.18 * height))
    r.add("belly", E(0.14 * bulk, 0.10 * bulk, 0.10), "orc_skin_dk", "chest", loc=(0, -0.02, 0.07))
    r.add("yoke", E(0.215 * bulk, 0.115 * bulk, 0.07), "orc_skin", "chest", loc=(0, 0, 0.33 * height))
    humanoid_arms(r, 0.34 * height, 0.20 * bulk, 0.22 * height, 0.21 * height, r_arm,
                  "orc_skin", "orc_skin_dk", flare=18)
    # Barely a neck, but the head has to clear the shoulders or it reads as a
    # green lump with arms: the first cut had the yoke drawn over the skull.
    r.joint("neck", (0, 0, 0.40 * height), "chest")
    r.joint("head", (0, -0.01, 0.10), "neck", rest=(-6, 0, 0))
    r.add("skull", E(0.125, 0.13, 0.12), "orc_skin", "head", loc=(0, 0, 0.07))
    r.add("brow", E(0.125, 0.065, 0.042), "orc_skin_dk", "head", loc=(0, -0.09, 0.115))
    r.add("jaw", E(0.10, 0.10, 0.055), "orc_skin", "head", loc=(0, -0.055, -0.015))
    for sx in (-1, 1):
        r.add("eye", E(0.024, 0.018, 0.024), "orc_eye", "head", loc=(sx * 0.052, -0.105, 0.075))
        # Ears back along the skull, and the tusks up out of the lower jaw.
        r.limb("ear", (sx * 0.105, 0.01, 0.07), (sx * 0.175, 0.07, 0.13), 0.026,
               "orc_skin_dk", "head", r_tip=0.005)
        r.limb("tusk", (sx * 0.050, -0.090, -0.030), (sx * 0.060, -0.105, 0.055), 0.019,
               "orc_tusk", "head", r_tip=0.005)
    return r


def build_orc1():
    """The grunt: lean, half-clothed, swinging a lump of wood."""
    r = _orc(bulk=0.92, height=0.95, r_leg=0.050, r_arm=0.044)
    r.add("belt", E(0.16, 0.12, 0.035), "orc_hide_dk", "pelvis", loc=(0, 0, 0.02))
    r.add("strap", E(0.05, 0.10, 0.20), "orc_hide", "chest", loc=(-0.06, -0.06, 0.20),
          rot=(0, 0.35, 0))
    # A club: a shaft with a knot on the end and a couple of nails in it.
    r.limb("club", (0, 0, -0.03), (0, 0, 0.40), 0.038, "orc_hide", "hand_r", r_tip=0.060)
    r.add("knot", E(0.090, 0.090, 0.10), "orc_hide", "hand_r", loc=(0, 0, 0.38))
    for k in (-1, 1):
        r.add("nail", E(0.018, 0.018, 0.04), "orc_steel", "hand_r", loc=(k * 0.07, 0, 0.38))
    r.pose.scale = (1.80, 1.80, 1.80)
    return r


def build_orc2():
    """The raider: leather, a shoulder plate, a cleaver and a round shield."""
    r = _orc(bulk=1.05, height=1.05, r_leg=0.056, r_arm=0.050)
    r.add("cuirass", E(0.185, 0.135, 0.15), "orc_hide", "chest", loc=(0, 0, 0.19))
    r.add("belt", E(0.175, 0.13, 0.04), "orc_hide_dk", "pelvis", loc=(0, 0, 0.02))
    r.add("kilt", E(0.155, 0.12, 0.09), "orc_hide_dk", "pelvis", loc=(0, 0, -0.06))
    # One pauldron, on the shield side, the way a raider would actually wear it.
    r.add("pauldron", E(0.095, 0.095, 0.06), "orc_iron", "shoulder_l", loc=(0, 0, 0.01))
    r.add("rivets", E(0.05, 0.05, 0.02), "orc_iron_dk", "shoulder_l", loc=(0, -0.04, 0.03))
    # A cleaver: a haft with a broad head on one side.
    r.limb("haft", (0, 0, -0.03), (0, 0, 0.34), 0.030, "orc_hide_dk", "hand_r", r_tip=0.026)
    r.add("blade", E(0.040, 0.105, 0.145), "orc_steel", "hand_r", loc=(0.055, 0, 0.31))
    r.add("edge", E(0.016, 0.090, 0.130), "orc_iron_dk", "hand_r", loc=(0.095, 0, 0.31))
    r.add("shield", E(0.145, 0.045, 0.145), "orc_hide", "hand_l", loc=(0, -0.05, -0.01))
    r.add("rim", E(0.155, 0.025, 0.155), "orc_iron_dk", "hand_l", loc=(0, -0.035, -0.01))
    r.add("boss", E(0.05, 0.035, 0.05), "orc_iron", "hand_l", loc=(0, -0.08, -0.01))
    r.pose.scale = (1.92, 1.92, 1.92)
    return r


def build_orc3():
    """The Warchief: a head taller, in iron, with a two-hander and the red of
    whatever he took it from over one shoulder."""
    r = _orc(bulk=1.22, height=1.18, r_leg=0.064, r_arm=0.058)
    r.add("cuirass", E(0.215, 0.155, 0.175), "orc_iron", "chest", loc=(0, 0, 0.21))
    r.add("plate", E(0.135, 0.06, 0.12), "orc_iron_dk", "chest", loc=(0, -0.11, 0.21))
    r.add("belt", E(0.20, 0.15, 0.045), "orc_hide_dk", "pelvis", loc=(0, 0, 0.02))
    r.add("buckle", E(0.05, 0.03, 0.045), "orc_gold", "pelvis", loc=(0, -0.11, 0.02))
    for side in ("l", "r"):
        r.add("pauldron", E(0.12, 0.12, 0.075), "orc_iron", "shoulder_" + side, loc=(0, 0, 0.01))
        r.add("spike", E(0.03, 0.03, 0.055), "orc_iron_dk", "shoulder_" + side, loc=(0, 0, 0.07))
    # The cape, hanging off the left shoulder down the back.
    r.add("cape", E(0.16, 0.05, 0.26), "orc_war_red", "chest", loc=(0, 0.11, 0.16))
    # A helm brow and a topknot, so the head reads as the biggest orc's.
    r.add("helm", E(0.115, 0.115, 0.055), "orc_iron", "head", loc=(0, 0.005, 0.125))
    r.limb("topknot", (0, 0.04, 0.16), (0, 0.16, 0.10), 0.030, "orc_hide_dk", "head", r_tip=0.008)
    # Two-hander: a long blade held in the right, the left brought across to it
    # by the poses.
    r.limb("blade", (0, 0, -0.02), (0, 0, 0.72), 0.058, "orc_steel", "hand_r", r_tip=0.026)
    r.add("edge", E(0.020, 0.020, 0.34), "orc_iron_dk", "hand_r", loc=(0.040, 0, 0.40))
    r.add("guard", E(0.125, 0.035, 0.028), "orc_gold", "hand_r", loc=(0, 0, -0.02))
    r.add("pommel", E(0.035, 0.035, 0.035), "orc_gold", "hand_r", loc=(0, 0, -0.08))
    r.pose.scale = (2.05, 2.05, 2.05)
    return r


# The three share their poses: the same body swinging whatever is in its hand.
def orc_idle(t):
    s = sn(t)
    return {"_z": 0.012 * s, "chest": X(3 * s), "head": (0, 0, 6 * sn(t, 0.3)),
            "shoulder_r": fwd(12), "elbow_r": X(-52), "hand_r": X(16),
            "shoulder_l": fwd(16), "elbow_l": X(-46)}


def orc_walk(t):
    v = gait(t, 30, 26, 20, 0.03, 8)
    v.update({"shoulder_r": fwd(14 + 8 * sn(t)), "elbow_r": X(-52), "hand_r": X(16),
              "shoulder_l": fwd(18 - 8 * sn(t)), "elbow_l": X(-46),
              "head": (0, 0, -4 * sn(t))})
    return v


def orc_attack(t):
    # Overhead: wound up behind the shoulder, then brought down through the
    # target with the hips behind it.
    i, k = phases(t, 0.38, 0.58, 1.0)
    wind = {"chest": (-8, 0, -22), "shoulder_r": fwd(-118), "elbow_r": X(-30), "hand_r": X(-10),
            "shoulder_l": fwd(-30), "head": (0, 0, -14), "_y": 0.06}
    down = {"chest": (16, 0, 26), "shoulder_r": fwd(64), "elbow_r": X(-6), "hand_r": X(24),
            "shoulder_l": fwd(34), "head": (0, 0, 16), "_y": -0.16, "_z": -0.02}
    return [mix({}, wind, k), mix(wind, down, k), mix(down, {}, k), {}][i]


def orc_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-16 * k), "_y": 0.14 * k, "head": (0, 0, 18 * k),
            "shoulder_r": fwd(-24 * k), "shoulder_l": fwd(-20 * k)}


def orc_death(t):
    return topple(t, armed=True)


# =================================================================================
#  The farm, and the mire
# =================================================================================
#
# Four animals that stand in a field and one that sits in the water. None of
# them fights: they graze, they scatter when hit, and what they leave behind is
# supper and a fleece. They are the same quadruped frame the boar and the deer
# are built on, in different proportions -- a cow is a barrel on short legs, a
# sheep is a cloud with a face, a pig is a barrel with a snout, and a chicken is
# two legs and an opinion.

bc.PALETTE.update({
    "cow_hide":   (0.94, 0.92, 0.88), "cow_patch": (0.24, 0.20, 0.19), "cow_udder": (0.88, 0.66, 0.66),
    "sheep_wool": (0.93, 0.91, 0.86), "sheep_wool_dk": (0.79, 0.76, 0.71), "sheep_face": (0.30, 0.28, 0.27),
    "pig_skin":   (0.88, 0.62, 0.60), "pig_skin_dk": (0.72, 0.46, 0.46), "pig_snout": (0.92, 0.72, 0.72),
    "hen_body":   (0.84, 0.66, 0.36), "hen_body_dk": (0.64, 0.46, 0.22), "hen_comb": (0.78, 0.22, 0.20),
    "hen_beak":   (0.90, 0.74, 0.28),
    "frog_skin":  (0.38, 0.60, 0.30), "frog_skin_dk": (0.26, 0.44, 0.22), "frog_belly": (0.84, 0.86, 0.62),
    # The pond at Fernhollow. A drake and a farm goose: the two of them have to
    # tell each other apart at forty pixels on green water, so one is dark at
    # the head and pale at the body and the other is the other way round.
    "duck_head":   (0.13, 0.35, 0.24), "duck_collar": (0.93, 0.92, 0.88),
    "duck_breast": (0.46, 0.28, 0.20), "duck_body":   (0.62, 0.60, 0.56),
    "duck_wing":   (0.40, 0.39, 0.37), "duck_bill":   (0.88, 0.72, 0.24),
    "duck_foot":   (0.88, 0.50, 0.16), "duck_speculum": (0.25, 0.33, 0.62),
    "goose_body":  (0.93, 0.92, 0.88), "goose_back":  (0.70, 0.68, 0.63),
    "goose_bill":  (0.91, 0.52, 0.18),
    "horn":       (0.84, 0.80, 0.68),
})


def build_cow():
    """A barrel on short legs, white with dark patches, with a wide flat head
    carried low. What says cow at thirty pixels is the patches and the width."""
    r = Rig()
    _quadruped(r, "cow_hide", "cow_hide", 0.44,
               chest=(0.24, 0.30, 0.23), hips=(0.22, 0.24, 0.22),
               leg_len=0.30, leg_r=0.055, fore_y=-0.18, hind_y=0.22, hip_x=0.14)
    # The patches, which are the whole of it at this size.
    for x, y, z, sx, sy in ((-0.10, -0.12, 0.10, 0.11, 0.09), (0.12, 0.06, 0.06, 0.10, 0.11),
                            (-0.06, 0.18, 0.08, 0.08, 0.07)):
        r.add("patch", E(sx, sy, 0.09), "cow_patch", "body", loc=(x, y, z))
    r.add("udder", E(0.09, 0.10, 0.06), "cow_udder", "body", loc=(0, 0.14, -0.13))
    r.joint("neck", (0, -0.30, 0.02), "body", rest=(24, 0, 0))
    r.joint("head", (0, -0.12, 0.0), "neck", rest=(-30, 0, 0))
    r.add("skull", E(0.10, 0.14, 0.09), "cow_hide", "head")
    r.add("muzzle", E(0.075, 0.07, 0.06), "cow_udder", "head", loc=(0, -0.18, -0.03))
    r.add("blaze", E(0.055, 0.08, 0.05), "cow_patch", "head", loc=(0, -0.06, 0.07))
    for sx in (-1, 1):
        r.add("ear", E(0.055, 0.03, 0.035), "cow_hide", "head", loc=(sx * 0.11, 0.0, 0.05), rot=(0, sx * -0.5, 0))
        r.add("eye", E(0.020, 0.018, 0.020), "beast_eye", "head", loc=(sx * 0.062, -0.11, 0.03))
        r.limb("horn", (sx * 0.06, -0.02, 0.08), (sx * 0.115, -0.05, 0.13), 0.016, "horn", "head", r_tip=0.006)
    r.joint("tail1", (0, 0.30, 0.14), "body", rest=(30, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.22), 0.014, "cow_hide", "tail1", r_tip=0.008)
    r.add("tuft", E(0.025, 0.025, 0.035), "cow_patch", "tail1", loc=(0, 0, -0.23))
    r.pose.scale = (2.1, 2.1, 2.1)
    return r


def build_sheep():
    """A cloud with a dark face and four thin legs under it. The fleece is one
    lumpy mass so that a shorn one would read differently, if it ever is."""
    r = Rig()
    _quadruped(r, "sheep_wool", "sheep_wool_dk", 0.34,
               chest=(0.19, 0.22, 0.19), hips=(0.18, 0.18, 0.18),
               leg_len=0.22, leg_r=0.035, fore_y=-0.12, hind_y=0.16, hip_x=0.10)
    for x, y, z, rad in ((-0.10, -0.06, 0.12, 0.10), (0.10, -0.02, 0.12, 0.10), (0.0, 0.10, 0.13, 0.11),
                         (0.0, -0.14, 0.10, 0.09), (-0.09, 0.14, 0.06, 0.08), (0.09, 0.14, 0.06, 0.08)):
        r.add("fleece", E(rad, rad * 0.95, rad * 0.9), "sheep_wool", "body", loc=(x, y, z))
    r.joint("neck", (0, -0.22, 0.06), "body", rest=(20, 0, 0))
    r.joint("head", (0, -0.09, 0.0), "neck", rest=(-24, 0, 0))
    r.add("skull", E(0.062, 0.095, 0.065), "sheep_face", "head")
    r.add("muzzle", E(0.042, 0.05, 0.038), "sheep_face", "head", loc=(0, -0.11, -0.03))
    r.add("cap", E(0.070, 0.06, 0.055), "sheep_wool", "head", loc=(0, 0.02, 0.06))
    for sx in (-1, 1):
        r.add("ear", E(0.045, 0.022, 0.026), "sheep_face", "head", loc=(sx * 0.075, -0.01, 0.02), rot=(0, sx * -0.6, 0))
        r.add("eye", E(0.016, 0.014, 0.016), "beast_eye", "head", loc=(sx * 0.040, -0.075, 0.02))
    r.joint("tail1", (0, 0.22, 0.10), "body", rest=(40, 0, 0))
    r.add("tail", E(0.035, 0.035, 0.045), "sheep_wool", "tail1", loc=(0, 0, -0.04))
    r.pose.scale = (1.8, 1.8, 1.8)
    return r


def build_pig():
    """Pink, low and long, with a snout you can see from the back."""
    r = Rig()
    _quadruped(r, "pig_skin", "pig_skin_dk", 0.30,
               chest=(0.17, 0.24, 0.17), hips=(0.16, 0.18, 0.16),
               leg_len=0.18, leg_r=0.042, fore_y=-0.13, hind_y=0.17, hip_x=0.10)
    r.joint("neck", (0, -0.24, 0.02), "body", rest=(12, 0, 0))
    r.joint("head", (0, -0.08, 0.0), "neck", rest=(-14, 0, 0))
    r.add("skull", E(0.085, 0.10, 0.080), "pig_skin", "head")
    r.add("snout", E(0.050, 0.075, 0.045), "pig_skin", "head", loc=(0, -0.14, -0.03))
    r.add("disc", E(0.048, 0.022, 0.042), "pig_snout", "head", loc=(0, -0.21, -0.03))
    for sx in (-1, 1):
        r.add("ear", E(0.032, 0.020, 0.050), "pig_skin_dk", "head", loc=(sx * 0.062, -0.03, 0.075),
              rot=(0.5, sx * -0.3, 0))
        r.add("eye", E(0.017, 0.015, 0.017), "beast_eye", "head", loc=(sx * 0.050, -0.075, 0.025))
    r.joint("tail1", (0, 0.24, 0.10), "body", rest=(20, 0, 0))
    r.limb("tail", (0, 0, 0), (0.03, 0.02, 0.06), 0.012, "pig_skin_dk", "tail1", r_tip=0.008)
    r.pose.scale = (1.9, 1.9, 1.9)
    return r


def build_chicken():
    """Two legs, a body and a comb. No quadruped frame: a hen is a different
    animal from the waist down."""
    r = Rig()
    r.joint("body", (0, 0, 0.20), rest=(-8, 0, 0))
    r.add("body", E(0.095, 0.13, 0.105), "hen_body", "body")
    r.add("wing_l", E(0.030, 0.10, 0.075), "hen_body_dk", "body", loc=(-0.090, 0.0, 0.01))
    r.add("wing_r", E(0.030, 0.10, 0.075), "hen_body_dk", "body", loc=(0.090, 0.0, 0.01))
    r.add("tail", E(0.055, 0.055, 0.085), "hen_body_dk", "body", loc=(0, 0.14, 0.08), rot=(-0.5, 0, 0))
    r.joint("neck", (0, -0.08, 0.07), "body", rest=(18, 0, 0))
    r.joint("head", (0, 0, 0.09), "neck", rest=(-20, 0, 0))
    r.add("skull", E(0.050, 0.055, 0.050), "hen_body", "head")
    r.add("comb", E(0.018, 0.045, 0.035), "hen_comb", "head", loc=(0, -0.01, 0.055))
    r.add("wattle", E(0.020, 0.022, 0.030), "hen_comb", "head", loc=(0, -0.045, -0.040))
    r.limb("beak", (0, -0.05, -0.005), (0, -0.105, -0.015), 0.022, "hen_beak", "head", r_tip=0.004)
    for sx in (-1, 1):
        r.add("eye", E(0.013, 0.012, 0.013), "beast_eye", "head", loc=(sx * 0.035, -0.035, 0.012))
        j = "leg_" + ("l" if sx < 0 else "r")
        r.joint(j, (sx * 0.045, 0.0, -0.09), "body", rest=(4, 0, 0))
        r.limb("shank", (0, 0, 0), (0, 0, -0.11), 0.016, "hen_beak", j, r_tip=0.013)
        r.add("foot", E(0.030, 0.045, 0.014), "hen_beak", j, loc=(0, -0.012, -0.115))
    r.pose.scale = (1.7, 1.7, 1.7)
    return r


def build_duck():
    """A mallard drake: dark green head, a white ring, a chestnut breast and a
    grey back, low and long on short legs set well back. The legs are short
    enough that the swim pose only has to tuck them."""
    r = Rig()
    r.joint("body", (0, 0, 0.15), rest=(-4, 0, 0))
    r.add("body", E(0.100, 0.165, 0.090), "duck_body", "body")
    r.add("breast", E(0.095, 0.075, 0.085), "duck_breast", "body", loc=(0, -0.070, -0.005))
    r.add("belly", E(0.085, 0.130, 0.040), "duck_collar", "body", loc=(0, 0.01, -0.050))
    for sx in (-1, 1):
        r.add("wing", E(0.028, 0.130, 0.070), "duck_wing", "body", loc=(sx * 0.082, 0.010, 0.012))
        r.add("flash", E(0.022, 0.040, 0.020), "duck_speculum", "body", loc=(sx * 0.084, 0.055, 0.020))
    r.add("tail", E(0.055, 0.070, 0.030), "duck_wing", "body", loc=(0, 0.150, 0.035), rot=(-0.40, 0, 0))
    r.joint("neck", (0, -0.085, 0.055), "body", rest=(14, 0, 0))
    r.add("throat", E(0.050, 0.050, 0.070), "duck_head", "neck", loc=(0, 0, 0.020))
    r.add("collar", E(0.054, 0.054, 0.018), "duck_collar", "neck", loc=(0, 0, -0.020))
    r.joint("head", (0, 0, 0.070), "neck", rest=(-14, 0, 0))
    r.add("skull", E(0.056, 0.070, 0.056), "duck_head", "head")
    r.limb("bill", (0, -0.030, -0.010), (0, -0.098, -0.020), 0.020, "duck_bill", "head", r_tip=0.016)
    r.add("bill_tip", E(0.036, 0.028, 0.012), "duck_bill", "head", loc=(0, -0.098, -0.020))
    for sx in (-1, 1):
        r.add("eye", E(0.013, 0.012, 0.013), "beast_eye", "head", loc=(sx * 0.030, -0.030, 0.014))
        j = "leg_" + ("l" if sx < 0 else "r")
        r.joint(j, (sx * 0.035, 0.030, -0.070), "body", rest=(6, 0, 0))
        r.limb("shank", (0, 0, 0), (0, 0, -0.058), 0.014, "duck_foot", j, r_tip=0.012)
        r.add("webbed", E(0.036, 0.050, 0.012), "duck_foot", j, loc=(0, -0.016, -0.062))
    r.pose.scale = (1.55, 1.55, 1.55)
    return r


def build_goose():
    """A farm goose: white, a grey mantle over the back, and the neck that is
    most of what you see of it. Bigger than the drake and paler, so the two
    read apart on the same water."""
    r = Rig()
    r.joint("body", (0, 0, 0.20), rest=(-5, 0, 0))
    r.add("body", E(0.125, 0.205, 0.115), "goose_body", "body")
    r.add("mantle", E(0.110, 0.150, 0.045), "goose_back", "body", loc=(0, 0.030, 0.075))
    for sx in (-1, 1):
        r.add("wing", E(0.032, 0.165, 0.090), "goose_body", "body", loc=(sx * 0.100, 0.015, 0.015))
        r.add("wing_edge", E(0.026, 0.070, 0.030), "goose_back", "body", loc=(sx * 0.102, 0.080, 0.010))
    r.add("tail", E(0.060, 0.070, 0.035), "goose_back", "body", loc=(0, 0.190, 0.055), rot=(-0.35, 0, 0))
    # The neck: two joints, so it can crane and curl.
    r.joint("neck", (0, -0.095, 0.080), "body", rest=(20, 0, 0))
    r.add("neck_low", E(0.055, 0.055, 0.105), "goose_body", "neck", loc=(0, 0, 0.045))
    r.joint("neck2", (0, 0, 0.105), "neck", rest=(-14, 0, 0))
    r.add("neck_up", E(0.048, 0.048, 0.090), "goose_body", "neck2", loc=(0, 0, 0.040))
    r.joint("head", (0, 0, 0.085), "neck2", rest=(-12, 0, 0))
    r.add("skull", E(0.054, 0.066, 0.056), "goose_body", "head")
    r.add("knob", E(0.040, 0.030, 0.030), "goose_bill", "head", loc=(0, -0.028, 0.030))
    r.limb("bill", (0, -0.028, -0.006), (0, -0.092, -0.014), 0.021, "goose_bill", "head", r_tip=0.017)
    for sx in (-1, 1):
        r.add("eye", E(0.013, 0.012, 0.013), "beast_eye", "head", loc=(sx * 0.029, -0.030, 0.016))
        j = "leg_" + ("l" if sx < 0 else "r")
        r.joint(j, (sx * 0.042, 0.035, -0.090), "body", rest=(6, 0, 0))
        r.limb("shank", (0, 0, 0), (0, 0, -0.072), 0.016, "duck_foot", j, r_tip=0.013)
        r.add("webbed", E(0.042, 0.058, 0.013), "duck_foot", j, loc=(0, -0.018, -0.078))
    r.pose.scale = (1.5, 1.5, 1.5)
    return r


def build_frog():
    """A wide mouth, two eyes on top and folded back legs. It sits."""
    r = Rig()
    r.joint("body", (0, 0, 0.09), rest=(-6, 0, 0))
    r.add("body", E(0.115, 0.135, 0.075), "frog_skin", "body")
    r.add("belly", E(0.095, 0.115, 0.045), "frog_belly", "body", loc=(0, 0.01, -0.045))
    for x, y in ((-0.05, -0.02), (0.06, 0.04), (-0.02, 0.08)):
        r.add("spot", E(0.030, 0.028, 0.02), "frog_skin_dk", "body", loc=(x, y, 0.06))
    r.joint("head", (0, -0.10, 0.03), "body", rest=(8, 0, 0))
    r.add("skull", E(0.095, 0.075, 0.055), "frog_skin", "head")
    r.add("mouth", E(0.090, 0.030, 0.016), "frog_skin_dk", "head", loc=(0, -0.055, -0.030))
    for sx in (-1, 1):
        r.add("brow", E(0.038, 0.036, 0.034), "frog_skin", "head", loc=(sx * 0.050, -0.010, 0.050))
        r.add("eye", E(0.026, 0.025, 0.026), "beast_eye", "head", loc=(sx * 0.050, -0.022, 0.062))
        # Back legs folded up beside it, and little front ones under the chin.
        j = "hind_" + ("l" if sx < 0 else "r")
        # The folded thigh is the whole of the frog's profile from the side, so
        # it is the darker green and it sits a little high and proud of the
        # body: from the east it is a haunch and not another inch of blob.
        r.joint(j, (sx * 0.100, 0.07, -0.005), "body", rest=(0, 0, 0))
        r.add("thigh", E(0.052, 0.075, 0.055), "frog_skin_dk", j, loc=(0, 0, 0))
        r.limb("shin", (0, 0.02, -0.02), (sx * 0.02, -0.09, -0.05), 0.026, "frog_skin_dk", j, r_tip=0.018)
        r.add("webbed", E(0.035, 0.045, 0.012), "frog_skin_dk", j, loc=(sx * 0.03, -0.13, -0.065))
        j2 = "fore_" + ("l" if sx < 0 else "r")
        r.joint(j2, (sx * 0.062, -0.08, -0.04), "body", rest=(0, 0, 0))
        r.limb("arm", (0, 0, 0), (sx * 0.012, -0.02, -0.055), 0.018, "frog_skin", j2, r_tip=0.012)
    r.pose.scale = (2.6, 2.6, 2.6)
    return r


# --- how they move -------------------------------------------------------------------
# Grazers: a head that dips and a body that breathes. None of them has an attack
# worth the name, so the "attack" clip is a butt or a peck, and hurt is a flinch.

def graze_idle(t):
    s = sn(t)
    return {"_z": 0.006 * s, "body": X(1.4 * s), "neck": X(4 * s + 4), "head": X(-6 * s - 3),
            "tail1": (0, 0, 9 * sn(t, 0.2))}


def graze_walk(t):
    swing = 22 * sn(t)
    return {"_z": 0.010 * abs(sn(t, 0.25)),
            "fore_l": fwd(swing), "fore_r": fwd(-swing),
            "hind_l": fwd(-swing), "hind_r": fwd(swing),
            "fore_l_knee": X(-10 - 8 * sn(t)), "fore_r_knee": X(-10 + 8 * sn(t)),
            "hind_l_knee": X(-12 + 8 * sn(t)), "hind_r_knee": X(-12 - 8 * sn(t)),
            "neck": X(5), "head": X(-4), "tail1": (0, 0, 14 * sn(t, 0.3))}


def graze_attack(t):
    i, k = phases(t, 0.45, 0.75, 1.0)
    lean = 0.0 if i == 0 else (26 * k if i == 1 else 26 * (1 - k))
    return {"_y": -0.05 * (lean / 26.0), "body": X(lean * 0.4), "neck": X(lean), "head": X(-lean * 0.6)}


def graze_hurt(t):
    k = 1.0 - t
    return {"_z": 0.02 * k, "body": X(-14 * k), "neck": X(-10 * k), "head": X(12 * k)}


def graze_death(t):
    return topple(t)


def hen_idle(t):
    s = sn(t)
    return {"_z": 0.004 * s, "neck": X(6 * s + 2), "head": X(-8 * s - 2 + 4 * sn(t, 0.3))}


def hen_walk(t):
    swing = 26 * sn(t)
    return {"_z": 0.012 * abs(sn(t, 0.25)),
            "leg_l": fwd(swing), "leg_r": fwd(-swing),
            "neck": X(10 * sn(t, 0.5) + 4), "head": X(-10 * sn(t, 0.5) - 2),
            "body": X(2 * sn(t))}


def hen_attack(t):
    i, k = phases(t, 0.4, 0.7, 1.0)
    peck = 0.0 if i == 0 else (34 * k if i == 1 else 34 * (1 - k))
    return {"neck": X(peck), "head": X(-peck * 0.5), "_y": -0.03 * (peck / 34.0)}


# Waterfowl. On land they are hens with longer necks; on the water the legs go
# away and the body settles, which is the whole of what tells a swimming bird
# from a standing one at this size.

def fowl_idle(t):
    s = sn(t)
    return {"_z": 0.004 * s, "body": X(1.2 * s), "neck": X(6 * s + 3),
            "head": X(-7 * s - 3 + 3 * sn(t, 0.3))}


def fowl_walk(t):
    swing = 24 * sn(t)
    # The waddle: the body rolls side to side, which is the whole joke of a
    # duck walking, and each leg takes its turn under it.
    return {"_z": 0.010 * abs(sn(t, 0.25)),
            "leg_l": fwd(swing), "leg_r": fwd(-swing),
            "_roll": 7 * sn(t, 0.25),
            "neck": X(8 * sn(t, 0.5) + 5), "head": X(-9 * sn(t, 0.5) - 3)}


def fowl_swim(t):
    s = sn(t)
    # Sat on the water: the body drops until the belly is the waterline, the
    # legs fold up under it out of sight, and the head goes side to side the
    # way a bird's does when it is going somewhere slowly.
    # The neck comes up out of the forward lean it walks with: a bird on the
    # water carries its head high, and on the goose -- whose neck is most of
    # what it is -- that is the difference between the two birds at this size.
    return {"_z": -0.055 + 0.006 * s, "body": X(-2 + 1.0 * s),
            "leg_l": X(-96), "leg_r": X(-96),
            "neck": X(-15 + 1.5 * s), "neck2": X(-4 + 1.0 * s),
            "head": (-2 * s + 6, 0, 7 * sn(t, 0.25))}


def fowl_attack(t):
    i, k = phases(t, 0.4, 0.7, 1.0)
    peck = 0.0 if i == 0 else (30 * k if i == 1 else 30 * (1 - k))
    return {"neck": X(peck), "head": X(-peck * 0.4), "_y": -0.025 * (peck / 30.0)}


def frog_idle(t):
    s = sn(t)
    # A frog at rest is a throat going in and out.
    return {"_z": 0.004 * s, "body": X(1.2 * s), "head": X(-2 * s)}


def frog_walk(t):
    # It hops: up, forward, down, and a beat sitting still.
    k = t % 1.0
    lift = max(0.0, math.sin(k * math.pi)) if k < 0.6 else 0.0
    return {"_z": 0.10 * lift, "_y": -0.03 * lift,
            "body": X(-16 * lift), "head": X(10 * lift),
            "hind_l": X(-30 * lift), "hind_r": X(-30 * lift),
            "fore_l": X(20 * lift), "fore_r": X(20 * lift)}


def frog_attack(t):
    i, k = phases(t, 0.4, 0.7, 1.0)
    out = 0.0 if i == 0 else (1.0 if i == 1 else 1.0 - k)
    return {"head": X(-10 * out), "_y": -0.02 * out}


def frog_hurt(t):
    k = 1.0 - t
    return {"_z": 0.02 * k, "body": X(-16 * k), "head": X(10 * k)}


# =================================================================================
#  The roster
# =================================================================================
CREATURES = {
    #             builder          frame  clips (idle, walk, attack, hurt, death)                      shadow radius
    "rat":       (build_rat,       48, (rat_idle, rat_walk, rat_attack, rat_hurt, rat_death),              0.34),
    "boar":      (build_boar,      48, (boar_idle, boar_walk, boar_attack, boar_hurt, boar_death),        0.46),
    "deer":      (build_deer,      64, (deer_idle, deer_walk, deer_attack, deer_hurt, deer_death),        0.40),
    "fox":       (build_fox,       48, (fox_idle, fox_walk, fox_attack, fox_hurt, fox_death),             0.34),
    "hare":      (build_hare,      48, (hare_idle, hare_walk, hare_attack, hare_hurt, hare_death),        0.26),
    "orc1":      (build_orc1,      64, (orc_idle, orc_walk, orc_attack, orc_hurt, orc_death),             0.30),
    "orc2":      (build_orc2,      64, (orc_idle, orc_walk, orc_attack, orc_hurt, orc_death),             0.34),
    "orc3":      (build_orc3,      64, (orc_idle, orc_walk, orc_attack, orc_hurt, orc_death),             0.38),
    "spider":    (build_spider,    48, (spider_idle, spider_walk, spider_attack, spider_hurt, spider_death), 0.50),
    "lizardman": (build_lizardman, 80, (liz_idle, liz_walk, liz_attack, liz_hurt, liz_death),            0.58),
    "ice_troll": (build_troll,     80, (troll_idle, troll_walk, troll_attack, troll_hurt, troll_death),  0.48),
    "wyvern":    (build_wyvern,   112, (wyv_idle, wyv_walk, wyv_attack, wyv_hurt, wyv_death),            0.70),
    "demon":     (build_demon,     80, (demon_idle, demon_walk, demon_attack, demon_hurt, demon_death),  0.40),
    "imp":       (build_imp,       48, (imp_idle, imp_walk, imp_attack, imp_hurt, imp_death),            0.22),
    "frost_dragon": (build_dragon, 144, (drake_idle, drake_walk, drake_attack, drake_hurt, drake_death), 0.92),
    "vask":      (build_vask,      64, (vask_idle, vask_walk, vask_idle, vask_idle, vask_idle),         0.52),
    "zombie":    (build_zombie,    64, (zom_idle, zom_walk, zom_attack, zom_hurt, zom_death),           0.36),
    "skeleton":  (build_skeleton,  64, (skel_idle, skel_walk, skel_attack, skel_hurt, skel_death),      0.30),
    "wraith":    (build_wraith,    64, (wraith_idle, wraith_walk, wraith_attack, wraith_hurt, wraith_death), 0.26),
    "slime":     (build_slime,     48, (slime_idle, slime_walk, slime_attack, slime_hurt, slime_death),  0.30),
    "bat":       (build_bat,       48, (bat_idle, bat_walk, bat_attack, bat_hurt, bat_death),            0.18),
    "hound":     (build_hound,     64, (hound_idle, hound_walk, hound_attack, hound_hurt, hound_death),  0.34),
    "ankou":     (build_ankou,     80, (ankou_idle, ankou_walk, ankou_attack, ankou_hurt, ankou_death),  0.30),
    "banshee":   (build_banshee,   72, (banshee_idle, banshee_walk, banshee_attack, banshee_hurt, banshee_death), 0.26),
    "wolf":      (build_wolf,      64, (hound_idle, hound_walk, hound_attack, hound_hurt, hound_death),  0.34),
    "bear":      (build_bear,      96, (bear_idle, bear_walk, bear_attack, bear_hurt, bear_death),       0.62),
    # The farm at Havenbrook, and what sits in the mire.
    "cow":       (build_cow,       80, (graze_idle, graze_walk, graze_attack, graze_hurt, graze_death),   0.56),
    "sheep":     (build_sheep,     64, (graze_idle, graze_walk, graze_attack, graze_hurt, graze_death),   0.42),
    "pig":       (build_pig,       56, (graze_idle, graze_walk, graze_attack, graze_hurt, graze_death),   0.40),
    "chicken":   (build_chicken,   40, (hen_idle, hen_walk, hen_attack, graze_hurt, graze_death),         0.20),
    "frog":      (build_frog,      40, (frog_idle, frog_walk, frog_attack, frog_hurt, graze_death),       0.22),
    # The pond at Fernhollow.
    "duck":      (build_duck,      40, (fowl_idle, fowl_walk, fowl_attack, graze_hurt, graze_death),     0.22),
    "goose":     (build_goose,     48, (fowl_idle, fowl_walk, fowl_attack, graze_hurt, graze_death),     0.26),
}
CLIP_FRAMES = [("idle", 4, True), ("walk", 6, True), ("attack", 6, False), ("hurt", 3, False),
               ("death", 6, False), ("run", 6, True), ("swim", 4, True)]

# Who runs. A creature that charges or bolts gets a run cycle; everything else
# has one gait and uses it, and a sheet it never plays is a sheet that quietly
# goes stale. These seven had run sheets left over from the art this project
# started with -- bright green orcs and spotted fawns, in a different style and
# a different layout from everything around them, and still being drawn the
# moment anything broke into a run.
# How far each one opens its walk up. A two-legged stride takes a lot of
# stretching before it reads as a run; four legs do not -- at anything much
# over a fifth the deer stops running and starts doing the splits.
# Pixels to stand a creature lower in its frame than the rest. Every rig is
# rendered with its feet about three quarters of the way down the cell, which
# leaves a strip of empty frame under the feet and less over the head. The
# Warchief is a head taller than the other orcs and his topknot reached past
# the top of the cell in the side and back views -- a few pixels of him drawn
# into the frame above, and cut off his own. Lowering him uses the empty strip
# instead of making him smaller.
FRAME_DROP = {"orc3": 8}

RUNNERS = {"orc1": 1.45, "orc2": 1.45, "orc3": 1.40,
           "boar": 1.20, "deer": 1.18, "fox": 1.20, "hare": 1.20,
           "wolf": 1.22, "bear": 1.15}

# Who floats. Like RUNNERS, a sheet only the creatures that need it get: the
# engine asks for "swim" and quietly keeps walking if the rig has no such clip,
# so this list is the whole of who can be drawn sitting on water.
SWIMMERS = {"duck": fowl_swim, "goose": fowl_swim}

# Who swims *in* the water rather than on it. A duck sits on the surface; a
# gator is under it to the eyes. For these the swim sheet is cut at the
# waterline -- a disc of holdout at the ground of each cell hides whatever is
# below it -- and throws no shadow, because nothing does on water.
WATERLINE = set()
FACINGS = bc.FACINGS


def keep_in_cell(rig, cell, right, up, creature, clip, facing, col):
    """Slide a posed rig back inside its cell if any of it has left.

    The engine draws exactly one cell of a sheet, so anything posed across a
    cell's edge is drawn twice: cut off in its own frame and as a scrap in the
    frame next door. Most poses never come near an edge. The ones that did were
    all the same kind of thing -- a four-legged animal rolled onto its side, a
    bat dropping out of the air, a spear thrust at the camera -- where the pose
    is right and only its position in the frame is wrong, and a pose authored
    per creature cannot know which of the four facings it is being drawn in.

    So this measures what was actually built, in the camera's own screen axes,
    and moves the whole rig by just enough to bring it back in. Nothing that
    already fits is touched, so every other sheet renders exactly as before; a
    rig too big for its cell is centred rather than pushed off the other side,
    and said so.
    """
    bpy.context.view_layer.update()
    lo_x = lo_y = float("inf")
    hi_x = hi_y = float("-inf")
    for ob in rig.parts:
        if ob.type != "MESH":
            continue
        m = ob.matrix_world
        for v in ob.data.vertices:
            w = m @ v.co
            sx, sy = w.dot(right), w.dot(up)
            lo_x, hi_x = min(lo_x, sx), max(hi_x, sx)
            lo_y, hi_y = min(lo_y, sy), max(hi_y, sy)
    if lo_x == float("inf"):
        return
    # The cell in the same screen axes. The camera sits a little over each
    # cell's ground point (setup_camera's lift), so the cell's middle is that
    # far up the screen from its grid position.
    half = bc.FRAME_SPAN / 2.0
    margin = UNITS_PER_PX * 2.0          # the outline pass adds a pixel
    elev = math.radians(bc.CAMERA_ELEVATION)
    cx = cell.dot(right)
    cy = cell.dot(up) + bc.FRAME_SPAN * 0.30 * math.cos(elev)

    def shift(lo, hi, centre):
        lo_edge, hi_edge = centre - half + margin, centre + half - margin
        if hi - lo > hi_edge - lo_edge:
            print("  ! %s %s %s frame %d is bigger than its cell" % (creature, clip, facing, col))
            return centre - (lo + hi) / 2.0
        if lo < lo_edge:
            return lo_edge - lo
        if hi > hi_edge:
            return hi_edge - hi
        return 0.0

    dx, dy = shift(lo_x, hi_x, cx), shift(lo_y, hi_y, cy)
    if dx or dy:
        rig.root.location = rig.root.location + right * dx + up * dy


def build_sheet(creature, clip_index):
    builder, frame_px, poses, shadow_r = CREATURES[creature]
    clip, frames, loops = CLIP_FRAMES[clip_index]
    # A run is made out of the creature's own walk rather than authored twice:
    # see running().
    if clip == "run":    pose_fn = running(poses[1], RUNNERS[creature])
    elif clip == "swim": pose_fn = SWIMMERS[creature]
    else:                pose_fn = poses[clip_index]

    bc.FRAME_PX = frame_px
    bc.FRAME_SPAN = UNITS_PER_PX * frame_px
    bc.clear_scene()
    bc._materials.clear()
    bc._meshes.clear()
    bc.setup_world()
    right, up = bc.camera_basis()

    bodies, shadows = [], []
    for row, (facing, turn) in enumerate(FACINGS):
        for col in range(frames):
            t = col / float(frames) if loops else col / float(max(1, frames - 1))
            rig = builder()
            offset = (right * (bc.FRAME_SPAN * col) - up * (bc.FRAME_SPAN * row)
                      - up * (UNITS_PER_PX * FRAME_DROP.get(creature, 0)))
            # apply() sets the root's rotation, turn included, so that a
            # world-space fall can sit outside the facing.
            rig.apply(pose_fn(t), turn)
            rig.root.location = offset + rig.fall
            cell = right * (bc.FRAME_SPAN * col) - up * (bc.FRAME_SPAN * row)
            keep_in_cell(rig, cell, right, up, creature, clip, facing, col)
            bodies += rig.parts
            if clip == "swim" and creature in WATERLINE:
                water = part("water", E(bc.FRAME_SPAN * 0.5, bc.FRAME_SPAN * 0.5, 0.0005), "shadow", None,
                             loc=rig.root.location.copy())
                water.is_holdout = True
                continue
            shadow = part("shadow", E(shadow_r, shadow_r * 0.7, 0.004), "shadow", None,
                          loc=offset + Vector((0, 0, 0.004)))
            shadows.append(shadow)

    bc.setup_camera(frames, len(FACINGS))
    bc.setup_render(frames, len(FACINGS))

    layers = []
    for name, objs, others in (("shadow", shadows, bodies), ("body", bodies, shadows)):
        for ob in objs:
            ob.hide_render = False
            ob.is_holdout = False
        for ob in others:
            ob.hide_render = name == "body"
            ob.is_holdout = name == "shadow"
        raw = os.path.join(RENDER_DIR, "%s_%s_%s.png" % (creature, clip, name))
        bc.render_to(raw)
        big = bc.read_png(raw)
        if name == "shadow":
            small = bc.reduce_majority(big, coverage_needed=8)
            small[..., :3] = 0
            small[..., 3] = np.where(small[..., 3] > 0, 72, 0)
        else:
            small = bc.outline(bc.reduce_majority(big))
        layers.append(small)

    flat = np.zeros_like(layers[0])
    for im in layers:
        a = im[..., 3:4].astype(np.float32) / 255.0
        flat[..., :3] = (im[..., :3] * a + flat[..., :3] * (1 - a)).astype(np.uint8)
        flat[..., 3] = np.maximum(flat[..., 3], im[..., 3])
    bc.write_png(os.path.join(OUT_ROOT, creature, "%s.png" % clip), flat)
    print("sheet %-10s %-7s %d frames x %d rows at %dpx" % (creature, clip, frames, len(FACINGS), frame_px))


def main():
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    clips = None
    if "--clips" in args:
        i = args.index("--clips")
        clips = args[i + 1].split(",")
        del args[i:i + 2]
    wanted = [a for a in args if a in CREATURES] or list(CREATURES)
    for creature in wanted:
        for index, (clip, _, _) in enumerate(CLIP_FRAMES):
            if clips and clip not in clips:
                continue
            if clip == "run" and creature not in RUNNERS:
                continue
            if clip == "swim" and creature not in SWIMMERS:
                continue
            build_sheet(creature, index)


# The monsters that fill the level ladder -- the Bayou's, Hollowrest Crypt's and
# the three past them -- are in tools/blender_bestiary.py. They are built from
# everything above, so this module is handed to it rather than imported by it:
# run by Blender this file is __main__, and importing it by name would run the
# whole of it a second time.
sys.modules.setdefault("blender_creatures", sys.modules[__name__])
import blender_bestiary  # noqa: E402

blender_bestiary.register()

# And what lives on Purgatory's Plateau, built from both of the above.
import blender_plateau  # noqa: E402

blender_plateau.register()


if __name__ == "__main__":
    main()
