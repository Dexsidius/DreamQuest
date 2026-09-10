# =============================================================================
#  blender_character.py - an original player character, rigged and rendered
#  straight into the sheet layout the game already reads.
#
#  Run headless:
#      blender --background --python tools/blender_character.py -- [clip ...]
#
#  The output convention is the one every character in this project already
#  uses: a square frame, four rows in the order down / left / right / up, one
#  column per frame, and the whole thing split into layers -- shadow, the
#  weapon behind the body, the body, the weapon in front, the head -- so worn
#  equipment and armour tinting keep working.
#
#  Two things make this fast enough to iterate on.
#
#  First, there is no armature. The character is a tree of empties with meshes
#  parented to them, and a pose is a dict of Euler angles applied to those
#  empties. Rigging a humanoid through bpy is a great deal of code to write and
#  debug for a figure that is twenty-five pixels tall; a joint hierarchy driven
#  by numbers is the same thing without the ceremony, and it is far easier to
#  read what a pose actually does.
#
#  Second, a whole sheet is one render. The camera is orthographic, so copies
#  of the character offset along the camera's own right and up vectors land on
#  an exact screen grid with no perspective error. One clip is therefore four
#  renders -- one per layer -- rather than one per frame per facing per layer,
#  which is the difference between a minute and half an hour.
# =============================================================================

import math
import os
import sys

import bpy
from mathutils import Vector, Euler

# --- output ------------------------------------------------------------------
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "assets", "_render", "character")

FRAME_PX = 64          # one animation frame, in game pixels
SUPERSAMPLE = 4        # rendered pixels per game pixel
# Measured against the rigs this character stands beside rather than picked.
# Their bodies read as seen from a little under fifty degrees; their faces are
# drawn front-on regardless, which is a flat-art cheat. HEAD_PITCH below is how
# that cheat is done in three dimensions.
CAMERA_ELEVATION = 46.0
HEAD_PITCH = 19.0          # degrees the head leans back, to show its face

# The frame is 64 wide but the character only occupies the middle of it, the
# same way the CraftPix rigs do -- the spare room is what lets a swing reach
# outside the body without being clipped.
FRAME_SPAN = 3.5       # world units across one frame


def to_linear(rgb):
    def one(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return tuple(one(c) for c in rgb)


# Deliberately light and low-contrast. The game multiplies the body layer by an
# armour colour, and a base that is already dark leaves nothing for the tint to
# do -- everything ends up black plate.
PALETTE = {
    "skin":     (0.902, 0.729, 0.580),
    "skin_dark":(0.796, 0.612, 0.478),
    "hair":     (0.400, 0.239, 0.145),
    "tunic":    (0.831, 0.784, 0.678),
    "tunic_alt":(0.612, 0.667, 0.549),
    "belt":     (0.353, 0.243, 0.169),
    "trouser":  (0.451, 0.376, 0.298),
    "boot":     (0.310, 0.216, 0.157),
    "steel":    (0.678, 0.706, 0.749),
    "grip":     (0.290, 0.196, 0.141),
    "eye":      (0.157, 0.192, 0.290),
    "shadow":   (0.05, 0.05, 0.07),
}


# --- scene plumbing ----------------------------------------------------------

def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.cameras,
                  bpy.data.lights, bpy.data.objects):
        for item in list(block):
            if item.users == 0:
                try:
                    block.remove(item)
                except Exception:
                    pass


def material(colour, emit=0.0):
    key = "m_%s_%.1f" % (colour, emit)
    if key in bpy.data.materials:
        return bpy.data.materials[key]
    mat = bpy.data.materials.new(key)
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    rgb = to_linear(PALETTE[colour])
    bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    bsdf.inputs["Roughness"].default_value = 0.85
    if "Specular IOR Level" in bsdf.inputs:
        bsdf.inputs["Specular IOR Level"].default_value = 0.15
    if emit:
        bsdf.inputs["Emission Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
        bsdf.inputs["Emission Strength"].default_value = emit
    return mat


def cube(name, size, loc, colour, parent=None, rot=(0, 0, 0),
         round_amount=0.02):
    """A box with its edges taken off. round_amount is in world units, so a
    generous value on the head turns a cube into something that reads as a
    skull rather than as a crate -- which is most of the difference between
    this looking like a character and looking like a stack of blocks."""
    bpy.ops.mesh.primitive_cube_add(size=1)
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = (size[0], size[1], size[2])
    ob.location = loc
    ob.rotation_euler = rot
    ob.data.materials.append(material(colour))
    m = ob.modifiers.new("bevel", "BEVEL")
    m.width = round_amount
    m.segments = 4 if round_amount > 0.05 else 2
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(40)
    if parent:
        ob.parent = parent
    return ob


def empty(name, loc, parent=None):
    e = bpy.data.objects.new(name, None)
    e.location = loc
    bpy.context.collection.objects.link(e)
    if parent:
        e.parent = parent
    return e


# --- the character -----------------------------------------------------------
# Chibi proportions, because that is what reads at this size: a head about a
# third of the total height, short limbs, and a silhouette wide enough that
# turning to face a different way actually changes the shape.
#
# The character is modelled facing -Y, which is toward the camera, so the
# "down" row is no rotation at all and the other three fall out of quarter
# turns. Getting this backwards renders the back of the head for every frame
# of the down-facing row, which looks exactly like a character with no face.

# How far the arms hang out from the body at rest, in degrees about Y. Poses
# add their own swing on top of this.
ARM_FLARE = 11.0

BODY_PARTS = "body"
HEAD_PARTS = "head"
WEAPON_PARTS = "weapon"


def build_character():
    """Returns (joints, groups). joints are the empties a pose rotates;
    groups maps a layer name to the objects that belong in it."""
    groups = {BODY_PARTS: [], HEAD_PARTS: [], WEAPON_PARTS: []}

    root = empty("root", (0, 0, 0))
    hips = empty("hips", (0, 0, 0.34), root)
    chest = empty("chest", (0, 0, 0.18), hips)
    neck = empty("neck", (0, 0, 0.24), chest)
    # The head leans back on its own pivot so its face is presented to the
    # camera while the body stays at the angle the rest of the game is drawn
    # at. Without this the only thing visible from above is the top of a hat.
    head_tilt = empty("head_tilt", (0, 0, 0), neck)
    head_tilt.rotation_euler = Euler((rad(-HEAD_PITCH), 0, 0), "XYZ")

    # Wide enough that an arm is its own shape in silhouette. At a torso
    # half-width of 0.19 a shoulder at 0.24 leaves one pixel of arm showing,
    # which is not an arm.
    shoulder_l = empty("shoulder_l", (-0.27, 0, 0.14), chest)
    shoulder_r = empty("shoulder_r", (0.27, 0, 0.14), chest)
    hip_l = empty("hip_l", (-0.11, 0, -0.04), hips)
    hip_r = empty("hip_r", (0.11, 0, -0.04), hips)

    # A short, wide torso. Almost all of the character's height is head.
    groups[BODY_PARTS] += [
        cube("torso", (0.38, 0.27, 0.40), (0, 0, 0.06), "tunic", chest,
             round_amount=0.05),
        cube("collar", (0.34, 0.26, 0.08), (0, 0, 0.25), "tunic_alt", chest),
        cube("belt", (0.40, 0.29, 0.08), (0, 0, -0.13), "belt", chest),
    ]

    # Arms are held a little away from the body so they exist in silhouette.
    # Tucked in, they merge with the torso and the character loses its arms.
    for side, sh, out in (("l", shoulder_l, 1.0), ("r", shoulder_r, -1.0)):
        elbow = empty("elbow_" + side, (0, 0, -0.17), sh)
        sh.rotation_euler = Euler((0, 0, 0), "XYZ")
        groups[BODY_PARTS] += [
            cube("upper_" + side, (0.13, 0.14, 0.20), (out * 0.02, 0, -0.09),
                 "tunic", sh),
            cube("fore_" + side, (0.12, 0.13, 0.18), (0, 0, -0.09), "skin", elbow),
        ]
        if side == "l":
            elbow_l = elbow
        else:
            elbow_r = elbow

    # Stubby legs. At this size a leg is three pixels of trouser and two of
    # boot, so there is no point modelling a knee joint's worth of detail --
    # but there is one, because a walk cycle needs the shin to trail.
    for side, hp in (("l", hip_l), ("r", hip_r)):
        knee = empty("knee_" + side, (0, 0, -0.16), hp)
        groups[BODY_PARTS] += [
            cube("thigh_" + side, (0.15, 0.16, 0.18), (0, 0, -0.08), "trouser", hp),
            cube("shin_" + side, (0.14, 0.15, 0.14), (0, 0, -0.07), "trouser", knee),
            cube("boot_" + side, (0.17, 0.21, 0.10), (0, 0.02, -0.17), "boot", knee),
        ]
        if side == "l":
            knee_l = knee
        else:
            knee_r = knee

    # The head, which is half the character. Built on the tilted pivot, so
    # everything here is in face-forward space: -Y is the face.
    groups[HEAD_PARTS] += [
        cube("head", (0.44, 0.42, 0.42), (0, 0, 0.24), "skin", head_tilt,
             round_amount=0.13),
        # Hair as a shell over the back and top with two tufts down the sides
        # of the face, which is how the hand-drawn characters frame theirs. A
        # plain cap on top is invisible from anywhere but directly overhead.
        cube("hair_cap", (0.47, 0.45, 0.20), (0, 0.02, 0.38), "hair", head_tilt,
             round_amount=0.09),
        cube("hair_back", (0.42, 0.13, 0.26), (0, 0.16, 0.26), "hair", head_tilt,
             round_amount=0.06),
        # Short tufts at the temples only. Run them down past the jaw and the
        # head becomes one brown mass in profile with no face left in it.
        cube("tuft_l", (0.07, 0.15, 0.16), (-0.19, -0.04, 0.27), "hair",
             head_tilt, round_amount=0.04),
        cube("tuft_r", (0.07, 0.15, 0.16), (0.19, -0.04, 0.27), "hair",
             head_tilt, round_amount=0.04),
        # Two dark blocks. At twenty-five pixels tall these are the whole face,
        # so they are deliberately large and set well apart.
        cube("eye_l", (0.09, 0.05, 0.11), (-0.10, -0.21, 0.23), "eye", head_tilt),
        cube("eye_r", (0.09, 0.05, 0.11), (0.10, -0.21, 0.23), "eye", head_tilt),
    ]

    # The sword, held in the right hand.
    hand_r = empty("hand_r", (0, 0, -0.18), elbow_r)
    groups[WEAPON_PARTS] += [
        cube("grip", (0.06, 0.06, 0.13), (0, 0, -0.05), "grip", hand_r),
        cube("guard", (0.19, 0.08, 0.05), (0, 0, -0.12), "steel", hand_r),
        cube("blade", (0.09, 0.05, 0.52), (0, 0, -0.40), "steel", hand_r),
    ]

    joints = {
        "root": root, "hips": hips, "chest": chest, "neck": neck,
        "shoulder_l": shoulder_l, "shoulder_r": shoulder_r,
        "elbow_l": elbow_l, "elbow_r": elbow_r,
        "hip_l": hip_l, "hip_r": hip_r,
        "knee_l": knee_l, "knee_r": knee_r,
        "hand_r": hand_r,
    }
    return joints, groups


def build_shadow():
    """A flat disc under the feet. It is its own layer because the game keeps
    the shadow out of every tint -- a character glowing as they charge an
    attack should not have a glowing shadow."""
    bpy.ops.mesh.primitive_cylinder_add(radius=0.30, depth=0.02,
                                        location=(0, 0, 0.01), vertices=24)
    ob = bpy.context.active_object
    ob.name = "shadow"
    ob.scale = (1.0, 0.62, 1.0)
    mat = bpy.data.materials.new("m_shadow_flat")
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    rgb = to_linear(PALETTE["shadow"])
    bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    bsdf.inputs["Roughness"].default_value = 1.0
    # Emissive, so it is the same flat grey whatever the lights are doing.
    bsdf.inputs["Emission Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    bsdf.inputs["Emission Strength"].default_value = 1.0
    ob.data.materials.append(mat)
    return ob


# --- poses -------------------------------------------------------------------
# A pose is degrees per joint per axis. Only what moves is listed; anything
# absent stays at rest. `t` runs 0..1 across the clip and wraps for loops, so a
# cycle is written as a sine rather than as a list of keyframes.

def rad(d):
    return math.radians(d)


def pose_idle(t):
    breathe = math.sin(t * math.tau)
    return {
        "chest": (rad(2 + breathe * 1.5), 0, 0),
        "shoulder_l": (rad(6 + breathe * 3), 0, rad(ARM_FLARE)),
        "shoulder_r": (rad(6 - breathe * 3), 0, rad(-ARM_FLARE)),
        "elbow_l": (rad(-14), 0, 0),
        "elbow_r": (rad(-18), 0, 0),
        "neck": (rad(-1 - breathe * 1.2), 0, 0),
        "hips": (0, 0, 0),
        "root_z": 0.006 * breathe,
    }


def pose_walk(t):
    # Exaggerated well past anatomy. A thigh is four pixels long here, so a
    # realistic twenty-degree stride moves a foot by less than one pixel and
    # the character appears to slide along the ground rather than walk.
    s = math.sin(t * math.tau)
    c = math.cos(t * math.tau)
    return {
        "hip_l": (rad(42 * s), 0, 0),
        "hip_r": (rad(-42 * s), 0, 0),
        "knee_l": (rad(-38 * max(0.0, -s)), 0, 0),
        "knee_r": (rad(-38 * max(0.0, s)), 0, 0),
        "shoulder_l": (rad(-34 * s), 0, rad(ARM_FLARE)),
        "shoulder_r": (rad(34 * s), 0, rad(-ARM_FLARE)),
        "elbow_l": (rad(-20), 0, 0),
        "elbow_r": (rad(-24), 0, 0),
        "chest": (rad(3), 0, rad(-4 * s)),
        "neck": (rad(-3), 0, 0),
        "root_z": 0.038 * abs(c) - 0.019,
    }


def pose_run(t):
    s = math.sin(t * math.tau)
    c = math.cos(t * math.tau)
    return {
        "hip_l": (rad(46 * s), 0, 0),
        "hip_r": (rad(-46 * s), 0, 0),
        "knee_l": (rad(-52 * max(0.0, -s) - 10), 0, 0),
        "knee_r": (rad(-52 * max(0.0, s) - 10), 0, 0),
        "shoulder_l": (rad(-52 * s), 0, rad(ARM_FLARE + 4)),
        "shoulder_r": (rad(52 * s), 0, rad(-ARM_FLARE - 4)),
        "elbow_l": (rad(-58), 0, 0),
        "elbow_r": (rad(-54), 0, 0),
        # Leaning into the run is most of what separates it from a walk at
        # this size; the legs alone are only a few pixels of difference.
        "chest": (rad(12), 0, rad(-5 * s)),
        "neck": (rad(-9), 0, 0),
        "root_z": 0.055 * abs(c) - 0.02,
    }


def pose_attack(t):
    # Wind up over the first third, swing through the middle, recover. Held a
    # beat at full extension so the frame that lands is the one you see.
    if t < 0.34:
        k = t / 0.34
        swing = -50 * k
        twist = -22 * k
        lean = -8 * k
    elif t < 0.62:
        k = (t - 0.34) / 0.28
        swing = -50 + 132 * k
        twist = -22 + 52 * k
        lean = -8 + 22 * k
    else:
        k = (t - 0.62) / 0.38
        swing = 82 - 62 * k
        twist = 30 - 24 * k
        lean = 14 - 12 * k
    return {
        "shoulder_r": (rad(swing), 0, rad(-10)),
        "elbow_r": (rad(-30 + swing * 0.25), 0, 0),
        "shoulder_l": (rad(18 - swing * 0.3), 0, rad(ARM_FLARE + 6)),
        "elbow_l": (rad(-40), 0, 0),
        "chest": (rad(lean), 0, rad(twist)),
        "neck": (rad(-lean * 0.5), 0, 0),
        "hip_l": (rad(-8), 0, 0),
        "hip_r": (rad(10), 0, 0),
        "knee_l": (rad(-12), 0, 0),
        "knee_r": (rad(-6), 0, 0),
    }


def pose_jump(t):
    # Crouch, launch, tuck, reach, land. The height curve is what sells it,
    # so the root lifts a long way and the legs fold under at the top.
    if t < 0.2:
        k = t / 0.2
        lift = -0.06 * k
        tuck = 30 * k
        arms = -20 * k
    elif t < 0.75:
        k = (t - 0.2) / 0.55
        lift = -0.06 + 0.62 * math.sin(k * math.pi) + 0.06 * k
        tuck = 30 - 20 * math.sin(k * math.pi)
        arms = -20 - 90 * math.sin(k * math.pi)
    else:
        k = (t - 0.75) / 0.25
        lift = 0.02 * (1.0 - k)
        tuck = 10 + 26 * math.sin(k * math.pi)
        arms = -110 * (1.0 - k) - 10
    return {
        "hip_l": (rad(tuck), 0, 0),
        "hip_r": (rad(tuck * 0.7), 0, 0),
        "knee_l": (rad(-tuck * 1.6), 0, 0),
        "knee_r": (rad(-tuck * 1.3), 0, 0),
        "shoulder_l": (rad(arms), 0, rad(ARM_FLARE + 6)),
        "shoulder_r": (rad(arms * 0.9), 0, rad(-ARM_FLARE - 6)),
        "elbow_l": (rad(-24), 0, 0),
        "elbow_r": (rad(-28), 0, 0),
        "chest": (rad(6 - tuck * 0.2), 0, 0),
        "neck": (rad(-4), 0, 0),
        "root_z": lift,
    }


def pose_hurt(t):
    k = math.sin(min(1.0, t * 1.4) * math.pi)
    return {
        "chest": (rad(-22 * k), 0, rad(10 * k)),
        "neck": (rad(16 * k), 0, 0),
        "shoulder_l": (rad(-30 * k), 0, rad(ARM_FLARE + 14 * k)),
        "shoulder_r": (rad(-26 * k), 0, rad(-ARM_FLARE - 10 * k)),
        "elbow_l": (rad(-40), 0, 0),
        "elbow_r": (rad(-44), 0, 0),
        "hip_l": (rad(-10 * k), 0, 0),
        "hip_r": (rad(8 * k), 0, 0),
        "root_z": -0.04 * k,
    }


def pose_death(t):
    # Falls backwards and stays down. The last frame is what sits on screen
    # while the corpse fades, so it has to read as a body, not as a pose.
    k = min(1.0, t * 1.15)
    e = k * k * (3 - 2 * k)      # smoothstep
    return {
        "root_pitch": rad(-84 * e),
        "root_z": -0.02 * e,
        "chest": (rad(14 * e), 0, 0),
        "neck": (rad(-20 * e), 0, 0),
        "shoulder_l": (rad(-60 * e), 0, rad(ARM_FLARE + 20 * e)),
        "shoulder_r": (rad(-50 * e), 0, rad(-ARM_FLARE - 16 * e)),
        "elbow_l": (rad(-20), 0, 0),
        "elbow_r": (rad(-24), 0, 0),
        "hip_l": (rad(18 * e), 0, 0),
        "hip_r": (rad(26 * e), 0, 0),
        "knee_l": (rad(-30 * e), 0, 0),
        "knee_r": (rad(-16 * e), 0, 0),
    }


# clip -> (pose function, frame count, loops)
CLIPS = {
    "idle":   (pose_idle,   6,  True),
    "walk":   (pose_walk,   6,  True),
    "run":    (pose_run,    8,  True),
    "attack": (pose_attack, 6,  False),
    "jump":   (pose_jump,   6,  False),
    "hurt":   (pose_hurt,   4,  False),
    "death":  (pose_death,  6,  False),
}

# Rows, in the order every sheet in this project uses. The value is how far the
# character turns from its modelled facing (+Y, away from the camera).
# A positive turn about Z takes the face direction -Y toward +X, which is
# screen right; so right is a quarter turn and left is three.
FACINGS = [("down", 0.0), ("left", 270.0), ("right", 90.0), ("up", 180.0)]


def apply_pose(joints, values):
    # head_tilt is deliberately not in `joints`, so resetting rotations here
    # never disturbs the fixed lean that shows the character's face.
    for name, joint in joints.items():
        joint.rotation_euler = Euler((0, 0, 0), "XYZ")
    joints["root"].location = Vector((0, 0, 0))

    for key, val in values.items():
        if key == "root_z":
            joints["root"].location.z += val
        elif key == "root_pitch":
            joints["root"].rotation_euler.x = val
        elif key in joints:
            joints[key].rotation_euler = Euler(val, "XYZ")


# --- rendering ---------------------------------------------------------------

def setup_world():
    world = bpy.data.worlds[0] if bpy.data.worlds else bpy.data.worlds.new("w")
    bpy.context.scene.world = world
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (0.38, 0.40, 0.46, 1.0)
    bg.inputs["Strength"].default_value = 1.15

    key = bpy.data.lights.new("key", "SUN")
    key.energy = 3.0
    key.color = (1.0, 0.95, 0.88)
    ob = bpy.data.objects.new("key", key)
    bpy.context.collection.objects.link(ob)
    ob.rotation_euler = (math.radians(52), 0, math.radians(-40))

    fill = bpy.data.lights.new("fill", "SUN")
    fill.energy = 1.0
    fill.color = (0.76, 0.84, 1.0)
    ob2 = bpy.data.objects.new("fill", fill)
    bpy.context.collection.objects.link(ob2)
    ob2.rotation_euler = (math.radians(64), 0, math.radians(140))


def camera_basis():
    """Right and up vectors of the camera, in world space. Offsetting an
    instance along these moves it exactly one way on screen and no other."""
    elev = math.radians(CAMERA_ELEVATION)
    right = Vector((1.0, 0.0, 0.0))
    up = Vector((0.0, math.sin(elev), math.cos(elev)))
    return right, up


def setup_camera(cols, rows):
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = FRAME_SPAN * cols
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.collection.objects.link(cam)
    bpy.context.scene.camera = cam

    right, up = camera_basis()
    # Aim at the middle of the grid. Frames are laid out with (0,0) at the
    # top-left cell, so the centre is half a grid across and down.
    centre = (right * (FRAME_SPAN * (cols - 1) / 2.0)
              - up * (FRAME_SPAN * (rows - 1) / 2.0)
              + Vector((0.0, 0.0, FRAME_SPAN * 0.30)))

    elev = math.radians(CAMERA_ELEVATION)
    back = Vector((0.0, -math.cos(elev), math.sin(elev))) * (FRAME_SPAN * cols * 3.0)
    cam.location = centre + back
    cam.rotation_euler = (math.radians(90.0 - CAMERA_ELEVATION), 0.0, 0.0)


def setup_render(cols, rows):
    scene = bpy.context.scene
    # EEVEE rather than Cycles: these are flat-shaded blocks that get reduced
    # to a few dozen pixels, and a path tracer buys nothing you can see while
    # costing about forty times as long over a whole sheet.
    for name in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE"):
        try:
            scene.render.engine = name
            break
        except TypeError:
            continue
    try:
        scene.eevee.taa_render_samples = 32
    except AttributeError:
        pass

    scene.render.resolution_x = FRAME_PX * SUPERSAMPLE * cols
    scene.render.resolution_y = FRAME_PX * SUPERSAMPLE * rows
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.filter_size = 0.7
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"


def render_to(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)


def build_sheet(clip_name):
    """Builds the whole grid for one clip and renders it once per layer."""
    pose_fn, frames, loops = CLIPS[clip_name]
    cols, rows = frames, len(FACINGS)

    clear_scene()
    setup_world()

    right, up = camera_basis()
    layers = {"shadow": [], "body": [], "head": [],
              "weapon_front": [], "weapon_back": []}

    for row, (facing, turn) in enumerate(FACINGS):
        for col in range(frames):
            # A looping clip divides the cycle evenly and never repeats the
            # first frame at the end; a one-shot runs to full extension.
            t = col / float(frames) if loops else col / float(frames - 1)

            joints, groups = build_character()
            apply_pose(joints, pose_fn(t))

            shadow = build_shadow()

            offset = right * (FRAME_SPAN * col) - up * (FRAME_SPAN * row)
            joints["root"].rotation_euler.z = math.radians(turn)
            joints["root"].location += offset
            shadow.location += offset

            layers["shadow"].append(shadow)
            layers["body"] += groups[BODY_PARTS]
            layers["head"] += groups[HEAD_PARTS]
            # The sword is drawn behind the character when they have their
            # back to us and in front otherwise, which is the whole reason the
            # game keeps two weapon layers.
            target = "weapon_back" if facing == "up" else "weapon_front"
            layers[target] += groups[WEAPON_PARTS]

    setup_camera(cols, rows)
    setup_render(cols, rows)

    order = ["shadow", "weapon_back", "body", "weapon_front", "head"]
    written = []
    for i, layer in enumerate(order):
        objects = layers[layer]
        if not objects:
            continue
        # Hide everything else. Rendering each layer separately is what keeps
        # the paperdoll working: the game recolours the body, hides the weapon
        # when your hands are empty, and never tints the shadow.
        for name, objs in layers.items():
            for ob in objs:
                ob.hide_render = (name != layer)

        path = os.path.join(OUT_DIR, "%s_%d_%s.png" % (clip_name, i + 1, layer))
        render_to(path)
        written.append(path)

    print("sheet %-7s %d frames x %d rows, %d layers"
          % (clip_name, frames, rows, len(written)))
    return written


def main():
    wanted = None
    if "--" in sys.argv:
        rest = sys.argv[sys.argv.index("--") + 1:]
        if rest:
            wanted = [c for c in rest if c in CLIPS]

    for name in (wanted or list(CLIPS)):
        build_sheet(name)


if __name__ == "__main__":
    main()
