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

    def apply(self, values):
        for name, e in self.j.items():
            if name == "pose":
                continue
            base = self.rest.get(name, (0, 0, 0))
            add = values.get(name, (0, 0, 0))
            e.rotation_euler = Euler(tuple(math.radians(base[i] + add[i]) for i in range(3)), "XYZ")
        self.pose.location = (values.get("_x", 0.0), values.get("_y", 0.0), values.get("_z", 0.0))
        self.pose.rotation_euler = Euler((math.radians(values.get("_pitch", 0.0)),
                                          math.radians(values.get("_roll", 0.0)), 0.0), "XYZ")


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


def fall_back(t, side=0):
    k = ease(t * 1.15)
    return {"_pitch": 84 * k, "_z": 0.10 * k, "_y": 0.25 * k, "hip_l": fwd(30 * k), "hip_r": fwd(12 * k),
            "shoulder_l": fwd(70 * k), "shoulder_r": fwd(60 * k), "neck": X(-20 * k)}


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
    thrust = {"shoulder_r": fwd(85), "elbow_r": X(-5), "hand_r": X(95), "chest": X(22), "_y": -0.26,
              "hip_l": fwd(40), "hip_r": fwd(-30), "knee_r": X(20), "tail1": (-10, 0, 14)}
    return [mix(rest, draw, k), mix(draw, thrust, k), mix(thrust, rest, k), rest][i]


def liz_hurt(t):
    k = math.sin(t * math.pi)
    return {"chest": X(-18 * k), "_y": 0.12 * k, "neck": X(-20 * k), "shoulder_r": fwd(6), "elbow_r": X(-14),
            "hand_r": X(4), "shoulder_l": fwd(-30 * k)}


def liz_death(t):
    v = fall_back(t)
    v.update({"tail1": (0, 0, 30 * ease(t))})
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
    k = ease(t * 1.1)
    return {"_pitch": -80 * k, "_z": 0.12 * k, "_y": -0.30 * k, "chest": X(10 * k),
            "shoulder_l": fwd(90 * k), "shoulder_r": fwd(80 * k), "hip_l": fwd(-10 * k)}


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
    v = fall_back(t)
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
    k = ease(t * 1.1)
    v = imp_hover(0, 10 * (1 - k))
    v.update({"_z": 0.30 * (1 - k), "_pitch": 80 * k, "_y": 0.2 * k, "wing_l": (0, 0, -60 * k), "wing_r": (0, 0, 60 * k)})
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
#  The roster
# =================================================================================
CREATURES = {
    #             builder          frame  clips (idle, walk, attack, hurt, death)                      shadow radius
    "rat":       (build_rat,       48, (rat_idle, rat_walk, rat_attack, rat_hurt, rat_death),              0.34),
    "spider":    (build_spider,    48, (spider_idle, spider_walk, spider_attack, spider_hurt, spider_death), 0.50),
    "lizardman": (build_lizardman, 80, (liz_idle, liz_walk, liz_attack, liz_hurt, liz_death),            0.58),
    "ice_troll": (build_troll,     80, (troll_idle, troll_walk, troll_attack, troll_hurt, troll_death),  0.48),
    "wyvern":    (build_wyvern,   112, (wyv_idle, wyv_walk, wyv_attack, wyv_hurt, wyv_death),            0.70),
    "demon":     (build_demon,     80, (demon_idle, demon_walk, demon_attack, demon_hurt, demon_death),  0.40),
    "imp":       (build_imp,       48, (imp_idle, imp_walk, imp_attack, imp_hurt, imp_death),            0.22),
    "frost_dragon": (build_dragon, 144, (drake_idle, drake_walk, drake_attack, drake_hurt, drake_death), 0.92),
    "vask":      (build_vask,      64, (vask_idle, vask_walk, vask_idle, vask_idle, vask_idle),         0.52),
}
CLIP_FRAMES = [("idle", 4, True), ("walk", 6, True), ("attack", 6, False), ("hurt", 3, False), ("death", 6, False)]
FACINGS = bc.FACINGS


def build_sheet(creature, clip_index):
    builder, frame_px, poses, shadow_r = CREATURES[creature]
    clip, frames, loops = CLIP_FRAMES[clip_index]
    pose_fn = poses[clip_index]

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
            rig.apply(pose_fn(t))
            offset = right * (bc.FRAME_SPAN * col) - up * (bc.FRAME_SPAN * row)
            rig.root.rotation_euler = (0, 0, math.radians(turn))
            rig.root.location = offset
            shadow = part("shadow", E(shadow_r, shadow_r * 0.7, 0.004), "shadow", None,
                          loc=offset + Vector((0, 0, 0.004)))
            bodies += rig.parts
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
            build_sheet(creature, index)


if __name__ == "__main__":
    main()
