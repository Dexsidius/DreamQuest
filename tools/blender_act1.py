# =============================================================================
#  blender_act1.py - what Act I sends at the player in the Reverie: the Hushed
#  that drift through dreaming Havenbrook, the Ashen Vanguard that drags itself
#  out of the square's Anchor (Elder Vask's nightmare), the Forge Demon in
#  Halda's dream forge, and the great Anchor in the Guild Hall wound round the
#  last Dawn Bell.
#
#  Rendered by tools/make_creatures.ps1 like every other monster:
#      .\tools\make_creatures.ps1 -Only hushed,ashen_vanguard
#
#  Built out of blender_creatures.py's parts with the prologue's Rig (two hands
#  on one hilt, joints moved, the whole squashed) and fitted to a height on
#  screen the way blender_bestiary.py's are; registered into the same
#  CREATURES table, and the bosses' own clips (a sprint, a sunder, a breath of
#  flame, a whirlwind) into EXTRA_CLIPS. blender_creatures.py hands itself over
#  at its foot.
#
#  The art direction is the screenplay's Appendix B. The references it names
#  -- the Heartless, the Smelter Demon -- are mood and archetype only; nothing
#  here is drawn from either design.
# =============================================================================

import math

import bmesh
import bpy
from mathutils import Vector

import blender_character as bc
import blender_creatures as cr
import blender_prologue as pr

E, C = cr.E, cr.C
X, fwd, sn, ease, phases, mix = cr.X, cr.fwd, cr.sn, cr.ease, cr.phases, cr.mix
TORUS = bc.mesh_torus
FRUSTUM = bc.mesh_frustum
Rig = pr.Rig


# --- colours ---------------------------------------------------------------------------
bc.PALETTE.update({
    # The Hushed: a cold pale grey that is not quite skin, shadowed violet by
    # the cel ramp, and the dream's lavender where they come apart into mist.
    "hush": (0.84, 0.83, 0.88), "hush_dk": (0.66, 0.64, 0.74), "hush_lt": (0.94, 0.94, 0.97),
    "hush_mist": (0.68, 0.64, 0.82), "hush_mist_dk": (0.50, 0.46, 0.66), "hush_void": (0.17, 0.13, 0.24),
    # The Ashen Vanguard: plate scorched black, charred scales gone the brown
    # of old blood, embers at every joint, a cloak burnt to rags and a blade
    # gone grey in the fire.
    # Lighter than scorched iron really is: drawn true, the whole of it was one
    # dark shape on the dream's dark ground, and the scales were not there.
    "av_plate": (0.33, 0.30, 0.30), "av_plate_dk": (0.20, 0.18, 0.19), "av_plate_lt": (0.54, 0.49, 0.46),
    "av_scale": (0.42, 0.29, 0.24), "av_scale_dk": (0.26, 0.18, 0.16), "av_scale_lt": (0.60, 0.42, 0.31),
    "av_ember_glow": (1.00, 0.50, 0.14), "av_coal": (0.56, 0.20, 0.09), "av_void": (0.05, 0.04, 0.05),
    "av_cloak": (0.15, 0.13, 0.15), "av_cloak_lt": (0.24, 0.21, 0.22), "av_singe": (0.46, 0.28, 0.15),
    "av_blade": (0.52, 0.51, 0.52), "av_blade_dk": (0.32, 0.31, 0.33), "av_blade_lt": (0.70, 0.68, 0.66),
    "av_grip": (0.24, 0.16, 0.12), "av_ash": (0.60, 0.58, 0.56), "av_ash_dk": (0.40, 0.38, 0.37),
    # The Forge Demon: blackened plate over a body of molten metal that shows
    # at every seam, the anvil's worn steel face for a helm, dents from the
    # hammer, and a blade of rough dark iron with the heat still in it.
    "fd_iron": (0.31, 0.29, 0.30), "fd_iron_dk": (0.19, 0.18, 0.19), "fd_iron_lt": (0.52, 0.50, 0.49),
    "fd_face": (0.66, 0.64, 0.62), "fd_glow": (1.00, 0.52, 0.12), "fd_hot_glow": (1.00, 0.86, 0.46),
    "fd_dent": (0.11, 0.10, 0.11), "fd_blade": (0.34, 0.32, 0.33), "fd_blade_lt": (0.56, 0.54, 0.53),
    "fd_grip": (0.22, 0.15, 0.12), "fd_steam": (0.90, 0.90, 0.93), "fd_steam_dk": (0.70, 0.70, 0.76),
    "fd_coal": (0.46, 0.15, 0.08),
    # The Anchor: black thread with a violet sheen where the light catches it,
    # dream smoke, and the pale bell glowing through the knot.
    "anc_thread": (0.11, 0.09, 0.14), "anc_thread_lt": (0.24, 0.20, 0.32), "anc_sheen": (0.44, 0.35, 0.60),
    "anc_smoke": (0.24, 0.18, 0.32), "anc_smoke_lt": (0.36, 0.28, 0.48),
    "anc_bell": (0.91, 0.89, 0.81), "anc_bell_dk": (0.72, 0.69, 0.63), "anc_bell_glow": (0.99, 0.95, 0.80),
})


# --- a rounded block -------------------------------------------------------------------
def rbox(w, d, h, round_=0.35):
    """A block w x d x h (full sizes) with its edges rounded off by `round_`
    (0 is a box, 1 an ellipsoid), centred on its origin. The Hushed and the
    plate are built from these: a box reads as a block, and a block with soft
    edges keeps its cel bands when it is reduced, where a hard one breaks into
    three flat faces and an outline."""
    key = ("rbox", w, d, h, round_)
    if key in bc._meshes:
        return bc._meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=16, radius=1.0)
    for v in bm.verts:
        n = Vector(v.co)
        m = max(abs(n.x), abs(n.y), abs(n.z), 1e-6)
        cube = n / m
        p = cube * (1.0 - round_) + n.normalized() * round_
        v.co = Vector((p.x * w / 2.0, p.y * d / 2.0, p.z * h / 2.0))
    me = bpy.data.meshes.new("rbox")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    bc._meshes[key] = me
    return me


def fitted(r, key, px, stance):
    """Rig.fit, and then every joint put back to its own size: the stance it
    is measured in may hide a sword or a puff of mist, and fit applies that
    stance to whichever copy of the rig is built first -- which then went into
    the sheet with its sword still hidden."""
    r.fit(key, px, stance)
    for name, ob in r.j.items():
        if name != "pose":
            ob.scale = (1.0, 1.0, 1.0)
    return r


# =================================================================================
#  The Hushed
# =================================================================================
def build_hushed():
    """Pale, faceless figures of the Reverie: a person stacked out of blocks a
    little too long in the limb, hung forward from the shoulders as if asleep
    on its feet, the head a smooth blank block tipped to one side, the arms
    dangling to the knee. They do not stand on anything: below the knee they
    thin out into mist, the hands end in it, and the rag of a sheet round the
    hips trails it behind them."""
    r = Rig()
    r.joint("base", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.68), "base")
    r.add("hips", rbox(0.21, 0.14, 0.12), "hush_dk", "pelvis")

    # The hem: the rag of a sheet round the hips, torn into short flaps --
    # longer behind -- each tip going to mist. Three rings of flaps so they
    # can stir out of step. Kept short: to the knee it was a dress, and the
    # figure a lady ghost.
    for name in ("hem_a", "hem_b", "hem_c"):
        r.joint(name, (0, 0, 0.0), "pelvis")
    for k in range(9):
        a = math.radians(-90 + k * 40 + (6 if k % 2 else -4))
        ring = ("hem_a", "hem_b", "hem_c")[k % 3]
        back = 0.5 + 0.5 * math.sin(a)                   # 0 in front, 1 behind
        length = 0.09 + 0.08 * back + (0.03 if k % 3 == 1 else 0.0)
        x, y = math.cos(a) * 0.110, math.sin(a) * 0.080
        r.add("flap", C(0.046, 0.026, length, squash_y=0.40), ("hush", "hush_dk")[k % 2], ring,
              loc=(x, y, 0.02), rot=(math.radians(12 + 12 * back), 0, a - math.pi / 2))
        if back > 0.4:
            # The tip comes apart: a wisp off the end of it, swept back.
            tip = Vector((math.cos(a) * (0.12 + 0.04 * back), math.sin(a) * (0.09 + 0.05 * back),
                          0.02 - length * 0.95))
            r.limb("flap_wisp", tuple(tip), tuple(tip + Vector((0, 0.06 + 0.08 * back, -0.05))), 0.022,
                   ("hush_mist", "hush_mist_dk")[k % 2], ring, r_tip=0.005)

    # A train of mist behind, off the small of the back.
    r.joint("trail", (0, 0.08, -0.06), "pelvis")
    for k, (x, ln, rr) in enumerate(((-0.06, 0.26, 0.034), (0.05, 0.32, 0.038), (0.0, 0.20, 0.028))):
        r.limb("trail_wisp", (x, 0.0, 0.0), (x * 1.5, ln, -0.18 - 0.05 * (k % 2)), rr,
               ("hush_mist", "hush_mist_dk")[k % 2], "trail", r_tip=0.005)

    # Legs: two blocks each, thinning to nothing below the knee, a little
    # apart so they read as legs and not as the hem.
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.080, 0, -0.04), "pelvis", rest=(0, sx * -3, 0))
        r.add("thigh", rbox(0.100, 0.100, 0.26), "hush", "hip_" + side, loc=(0, 0, -0.13))
        r.joint("knee_" + side, (0, 0, -0.27), "hip_" + side, rest=(6, 0, 0))
        r.add("shin", rbox(0.082, 0.082, 0.20), "hush_dk", "knee_" + side, loc=(0, 0, -0.09))
        r.limb("ankle_wisp", (0, 0, -0.18), (0, 0.08, -0.33), 0.036, "hush_mist", "knee_" + side, r_tip=0.006)
        r.limb("ankle_wisp2", (0, 0.01, -0.16), (sx * 0.03, 0.12, -0.25), 0.024, "hush_mist_dk", "knee_" + side,
               r_tip=0.005)

    # Slumped rather than hunched: the chest only a little forward, the
    # shoulders rolled down and in. Bent over much further, the camera saw the
    # crown of the head and none of the blank face under it.
    r.joint("chest", (0, 0, 0.07), "pelvis", rest=(9, 0, 0))
    r.add("belly", rbox(0.16, 0.115, 0.13), "hush_dk", "chest", loc=(0, 0, 0.07))
    # A dark seam between the blocks of the body, like the joint of a doll.
    r.add("waist_gap", rbox(0.15, 0.11, 0.05), "hush_void", "chest", loc=(0, 0, 0.145))
    r.add("ribs", rbox(0.24, 0.155, 0.17), "hush", "chest", loc=(0, 0, 0.245))
    r.add("shoulders", rbox(0.30, 0.13, 0.065), "hush", "chest", loc=(0, 0.01, 0.32))

    # Long arms, three blocks each, dangling past the knee and held a little
    # out from the body so they show; the fingers are long and pale and come
    # apart at the ends.
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.170, 0, 0.29), "chest", rest=(0, sx * -11, 0))
        r.add("upper", rbox(0.072, 0.072, 0.26), "hush", "shoulder_" + side, loc=(0, 0, -0.12))
        r.joint("elbow_" + side, (0, 0, -0.26), "shoulder_" + side, rest=(-8, 0, 0))
        r.add("fore", rbox(0.062, 0.062, 0.25), "hush_dk", "elbow_" + side, loc=(0, 0, -0.12))
        r.joint("hand_" + side, (0, 0, -0.26), "elbow_" + side)
        r.add("hand", rbox(0.078, 0.048, 0.10), "hush_lt", "hand_" + side, loc=(0, 0, -0.04))
        for k in range(3):
            o = k - 1.0
            r.limb("finger", (o * 0.024, -0.005, -0.08), (o * 0.042, -0.015, -0.22), 0.016, "hush",
                   "hand_" + side, r_tip=0.009)
            r.limb("finger_wisp", (o * 0.042, -0.015, -0.21), (o * 0.050, 0.02, -0.29), 0.011,
                   "hush_mist", "hand_" + side, r_tip=0.004)

    # The head: a smooth block, taller than it is wide, tipped over onto one
    # shoulder, and nothing on it -- a face plane a shade paler than the rest,
    # so there is somewhere a face ought to be.
    r.joint("neck", (0, -0.02, 0.35), "chest", rest=(-6, 0, 0))
    # The neck in the body's shade, not dark: dark, it showed under the tipped
    # head as a mouth -- a feature, on the one thing that must have none.
    r.add("neckp", rbox(0.070, 0.070, 0.15), "hush_dk", "neck", loc=(0, 0, 0.05))
    r.joint("head", (0, -0.01, 0.13), "neck", rest=(-2, 18, 0))
    r.add("skull", rbox(0.21, 0.19, 0.26, 0.55), "hush", "head", loc=(0, 0, 0.12))
    r.add("face", rbox(0.165, 0.03, 0.20, 0.6), "hush_lt", "head", loc=(0, -0.090, 0.11))

    # What it comes apart into: puffs round where the body was, each on a
    # joint of its own so it can swell, drift and thin on its own, and kept
    # at nothing until the death grows them.
    r.joint("mist", (0, 0, 0), "pose")
    for k, (x, y, z, rr) in enumerate(HUSH_PUFFS):
        r.joint("mp%d" % k, (x, y, z), "mist")
        r.add("puff", E(rr, rr * 0.85, rr * 0.75), ("hush_mist", "hush_mist_dk", "hush")[k % 3], "mp%d" % k)
    return fitted(r, "hushed", 34, HUSH_REST)


# Where the puffs of a Hushed coming apart start: over the head and shoulders,
# along the arms, round the hips and down the legs -- the figure's shape, as
# a cloud -- and how big each is.
HUSH_PUFFS = ((-0.05, 0.00, 1.24, 0.10), (0.08, -0.02, 1.12, 0.12), (-0.18, 0.02, 1.02, 0.10),
              (0.20, 0.00, 0.98, 0.11), (0.00, 0.04, 0.92, 0.14), (-0.26, -0.02, 0.70, 0.09),
              (0.27, 0.01, 0.66, 0.09), (-0.06, -0.03, 0.70, 0.13), (0.09, 0.05, 0.60, 0.12),
              (-0.10, 0.02, 0.38, 0.11), (0.12, -0.02, 0.32, 0.10), (0.00, 0.08, 0.16, 0.12),
              (-0.20, 0.06, 0.14, 0.08), (0.22, 0.04, 0.18, 0.07))


def _puffs(size, rise=0.0, spread=0.0, jitter=0.0):
    """The puffs at `size` (0 is gone, 1 their own size), lifted by `rise` and
    pushed out from the body's middle by `spread`, each a little differently."""
    v = {}
    for k, (x, y, z, rr) in enumerate(HUSH_PUFFS):
        w = 1.0 + jitter * math.sin(k * 2.39)                 # each its own share
        s = max(0.0, size * (1.0 + 0.35 * jitter * math.cos(k * 1.7)))
        v["~mp%d" % k] = -0.99 + 0.99 * s
        out = Vector((x, y, 0.0))
        out = out.normalized() if out.length > 1e-4 else Vector((0.0, 1.0, 0.0))
        v["@mp%d" % k] = (out.x * spread * w, out.y * spread * w + 0.04 * spread, rise * w * (0.6 + z * 0.5))
    return v


# Hanging: the arms down and a little forward, the hands at the knee.
HUSH_REST = {"shoulder_l": fwd(10), "shoulder_r": fwd(6), "elbow_l": X(-6), "elbow_r": X(-10)}
HUSH_REST.update(_puffs(0.0))


def _hush(**more):
    v = dict(HUSH_REST)
    v.update(more)
    return v


def hush_idle(t):
    # A slow hover and the head rocking a little on its tilt; the arms hang
    # and sway as the body does, and the rags stir.
    s, c = sn(t), sn(t, 0.25)
    return _hush(_z=0.018 + 0.014 * s, chest=X(2 * s), head=(2 * c, 3 * s, 0),
                 shoulder_l=fwd(10 + 4 * c), shoulder_r=fwd(6 - 4 * c), elbow_l=X(-6 - 3 * s), elbow_r=X(-10 + 3 * s),
                 hem_a=(4 * s, 0, 5 * c), hem_b=(-3 * c, 0, -4 * s), hem_c=(3 * c, 0, 3 * s),
                 trail=(6 * s, 0, 6 * c), hip_l=fwd(4 * s), hip_r=fwd(-4 * s))


def hush_walk(t):
    # A slow drifting advance: the legs barely step, swinging from the hip as
    # if dragged; the body rides forward over them and the arms trail behind,
    # and the mist streams back.
    s, c = sn(t), sn(t, 0.25)
    return _hush(_z=0.020 + 0.014 * abs(c), chest=(12 + 2 * c, 6 * s, 0), pelvis=(0, -4 * s, 0),
                 head=(-4 + 3 * c, 3 * s, 0),
                 hip_l=fwd(22 * s), hip_r=fwd(-22 * s), knee_l=X(28 * max(0.0, -s)), knee_r=X(28 * max(0.0, s)),
                 shoulder_l=fwd(-6 - 8 * s), shoulder_r=fwd(-10 + 8 * s), elbow_l=X(-8), elbow_r=X(-12),
                 hem_a=(12 + 4 * s, 0, 4 * c), hem_b=(10 - 3 * c, 0, -4 * s), hem_c=(14 + 3 * c, 0, 3 * s),
                 trail=(-24 + 6 * s, 0, 8 * c))


# The lunge: the head comes up off its shoulder, the arms swing up and out,
# and it falls forward onto whatever is in front of it, raking down.
HUSH_WIND = _hush(chest=X(-8), neck=X(-12), head=(-14, 0, 0), _z=0.05,
                  shoulder_l=(-130, 30, 0), shoulder_r=(-130, -30, 0), elbow_l=X(-30), elbow_r=X(-30),
                  hem_a=(-10, 0, 0), trail=(10, 0, 0))
HUSH_RAKE = _hush(chest=X(38), neck=X(-8), head=(-10, 0, 0), _y=-0.12, _z=0.0,
                  shoulder_l=(-40, 10, 0), shoulder_r=(-40, -10, 0), elbow_l=X(-4), elbow_r=X(-4),
                  hip_l=fwd(26), hip_r=fwd(-18), knee_r=X(20), hem_a=(20, 0, 0), hem_b=(16, 0, 0),
                  hem_c=(22, 0, 0), trail=(-30, 0, 0))


def hush_attack(t):
    frame = int(round(t * 5))
    rest = hush_idle(0.0)
    if frame == 0:
        return rest
    if frame == 1:
        return mix(rest, HUSH_WIND, 0.7)
    if frame == 2:
        return HUSH_WIND
    if frame == 3:
        return mix(HUSH_WIND, HUSH_RAKE, 0.75)
    if frame == 4:
        return HUSH_RAKE
    return mix(HUSH_RAKE, rest, 0.6)


def hush_hurt(t):
    k = math.sin(t * math.pi)
    return _hush(chest=X(20 - 30 * k), neck=X(-10 * k), head=(-16 * k, -18 * k, 0), _y=0.08 * k, _z=0.02,
                 shoulder_l=fwd(10 - 30 * k), shoulder_r=fwd(6 - 26 * k), elbow_l=X(-6 - 20 * k),
                 elbow_r=X(-10 - 20 * k), hem_a=(-14 * k, 0, 0), trail=(14 * k, 0, 0))


# The dissolve, frame by frame: (how much of the body is left, how big the
# puffs are, how far they have risen, how far they have spread).
HUSH_DISSOLVE = ((1.00, 0.00, 0.00, 0.00), (0.80, 0.55, 0.00, 0.02), (0.42, 1.00, 0.04, 0.06),
                 (0.00, 1.05, 0.12, 0.14), (0.00, 0.62, 0.26, 0.26), (0.00, 0.26, 0.42, 0.36))


def hush_death(t):
    """It comes apart: thrown back, then sagging, and as it sags it thins into
    the lavender mist it trails -- a cloud the shape of it for a moment, which
    rises off where it was, spreads and goes."""
    frame = min(5, int(round(t * 5)))
    body, size, rise, spread = HUSH_DISSOLVE[frame]
    k = ease(t * 1.6)
    v = _hush(chest=X(20 + 20 * k), neck=X(14 * k), head=(-20 + 50 * k, 20 * k, 0),
              shoulder_l=fwd(10 - 30 * k), shoulder_r=fwd(6 - 26 * k), elbow_l=X(-6 - 24 * k),
              elbow_r=X(-10 - 20 * k), knee_l=X(30 * k), knee_r=X(24 * k), hem_a=(20 * k, 0, 0),
              trail=(30 * k, 0, 0), _z=-0.06 * k)
    # The body sinks into itself towards the ground as the cloud takes it.
    v["~base"] = -0.99 * (1.0 - body)
    v.update(_puffs(size, rise, spread, jitter=0.5))
    return v


# =================================================================================
#  The Ashen Vanguard
# =================================================================================
def _scales(r, parent, rows, cols, x0, x1, z0, dz, y, size, curve=0.0, colours=("av_scale", "av_scale_lt")):
    """Rows of overlapping scales over a surface, alternate rows offset by half
    a scale: dragon-scale plate, at forty pixels a texture of small bumps
    rather than a list of scales anyone counts. `curve` bends a row back round
    the body at its ends."""
    for i in range(rows):
        n = cols - (i % 2)
        for k in range(n):
            f = (k + 0.5 * (i % 2)) / max(1, cols - 1)
            x = x0 + (x1 - x0) * f
            yy = y + curve * (2.0 * f - 1.0) ** 2
            r.add("scale", E(size, size * 0.45, size * 0.80), colours[(i + k) % 2], parent,
                  loc=(x, yy, z0 + i * dz))


def build_ashen_vanguard():
    """Elder Vask's nightmare, dragged out of the square's Anchor: a hollow
    suit of scorched armour plated in dark charred scales, ember light where
    the joints are cracked, a visor split across with the dark and a glow
    behind it, the rags of a cloak burnt black, and a two-handed greatsword
    notched along both edges. A veteran's battered harness after the last
    hunt, not a monster's hide; nothing inside it but ash, which drifts up out
    of the neck."""
    r = Rig()
    r.joint("base", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.74), "base")
    r.add("hips", E(0.20, 0.15, 0.12), "av_plate_dk", "pelvis")
    cr.humanoid_legs(r, -0.04, 0.125, 0.33, 0.33, 0.085, "av_plate", "av_plate_dk", foot_col="av_plate_dk")
    for side in ("l", "r"):
        r.add("cuisse", C(0.098, 0.088, 0.20), "av_scale", "hip_" + side, loc=(0, -0.005, -0.02))
        r.add("knee_glow", E(0.095, 0.060, 0.045), "av_ember_glow", "knee_" + side, loc=(0, 0.0, 0.0))
        r.add("poleyn", E(0.080, 0.072, 0.062), "av_plate_lt", "knee_" + side, loc=(0, -0.05, 0.01))
        r.add("greave", C(0.086, 0.076, 0.20), "av_plate", "knee_" + side, loc=(0, -0.005, -0.05))
        r.add("sabaton", E(0.090, 0.130, 0.055), "av_plate_dk", "knee_" + side, loc=(0, -0.07, -0.33))
    # The fauld: a skirt of scale, and the ember showing at the waist above it.
    r.add("waist_glow", TORUS(0.175, 0.030), "av_ember_glow", "pelvis", loc=(0, 0, 0.085))
    r.add("fauld", C(0.215, 0.255, 0.15, squash_y=0.80), "av_scale_dk", "pelvis", loc=(0, 0, 0.06))
    for i in range(2):
        for k in range(9):
            a = math.radians(-90 + (k - 4) * 21 + (10 if i else 0))
            r.add("tasset", E(0.055, 0.025, 0.060), ("av_scale", "av_scale_lt")[(i + k) % 2], "pelvis",
                  loc=(math.cos(a) * (0.225 + 0.025 * i), math.sin(a) * (0.185 + 0.02 * i), -0.02 - 0.06 * i),
                  rot=(math.radians(-14), 0, a + math.pi / 2))

    # The cuirass, bent forward over the fight.
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(9, 0, 0))
    r.add("cuirass", E(0.255, 0.185, 0.26), "av_plate", "chest", loc=(0, 0, 0.25))
    r.add("plackart", E(0.20, 0.13, 0.10), "av_plate_dk", "chest", loc=(0, -0.07, 0.10))
    _scales(r, "chest", 3, 5, -0.13, 0.13, 0.22, 0.075, -0.165, 0.040, curve=0.035)
    # A crack across the breast with the ember behind it, and a dent.
    for x, z, a in ((-0.10, 0.40, 0.5), (-0.03, 0.36, -0.2), (0.05, 0.40, 0.7)):
        r.add("crack", E(0.055, 0.014, 0.013), "av_ember_glow", "chest", loc=(x, -0.186, z), rot=(0, a, 0))
    r.add("dent", E(0.035, 0.012, 0.030), "av_plate_dk", "chest", loc=(0.12, -0.165, 0.14))
    r.add("gorget", E(0.165, 0.145, 0.065), "av_plate_dk", "chest", loc=(0, 0, 0.50))
    r.add("hollow", E(0.090, 0.080, 0.035), "av_void", "chest", loc=(0, -0.01, 0.540))
    r.add("neck_glow", E(0.060, 0.050, 0.020), "av_ember_glow", "chest", loc=(0, -0.01, 0.552))

    # The cloak: a mantle over the shoulders and rags of it down the back to
    # the calf, singed brown at the ends, on a joint of its own so a sprint can
    # stream it.
    r.add("mantle", E(0.28, 0.20, 0.09), "av_cloak", "chest", loc=(0, 0.04, 0.45))
    r.joint("cloak", (0, 0.17, 0.44), "chest", rest=(6, 0, 0))
    for k, (x, ln) in enumerate(((-0.21, 0.66), (-0.14, 0.86), (-0.07, 0.74), (0.0, 0.92), (0.07, 0.70),
                                 (0.14, 0.84), (0.21, 0.60))):
        r.add("rag", C(0.064, 0.044, ln, squash_y=0.30), "av_cloak_lt" if k in (2, 5) else "av_cloak", "cloak",
              loc=(x, 0.0, 0.0), rot=(0, math.radians(x * 30), 0))
        r.add("singe", E(0.040, 0.016, 0.040), "av_singe", "cloak",
              loc=(x + math.sin(math.radians(x * 30)) * -ln, 0.0, -ln - 0.01))

    # Pauldrons of overlapping scale, the ember in the gap under each.
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("pauldron_" + side, (sx * 0.26, 0, 0.45), "chest")
        r.add("shoulder_glow", E(0.080, 0.095, 0.045), "av_ember_glow", "pauldron_" + side, loc=(sx * 0.03, 0, -0.085))
        r.add("pauldron", E(0.150, 0.135, 0.095), "av_plate", "pauldron_" + side, loc=(sx * 0.025, 0, 0.0))
        r.add("pauldron_lame", E(0.135, 0.125, 0.060), "av_scale", "pauldron_" + side, loc=(sx * 0.035, 0, -0.06))
        r.add("pauldron_spine", E(0.030, 0.050, 0.045), "av_scale_lt", "pauldron_" + side, loc=(sx * 0.05, 0, 0.09))
    cr.humanoid_arms(r, 0.43, 0.27, 0.27, 0.26, 0.066, "av_plate", "av_plate_dk", flare=8)
    for side in ("l", "r"):
        r.add("elbow_glow", E(0.078, 0.060, 0.042), "av_ember_glow", "elbow_" + side, loc=(0, 0.0, 0.0))
        r.add("couter", E(0.064, 0.060, 0.052), "av_plate_lt", "elbow_" + side, loc=(0, 0.035, 0.0))
        r.add("vambrace", C(0.068, 0.062, 0.16), "av_scale", "elbow_" + side, loc=(0, 0, -0.04))
        r.add("gauntlet", E(0.076, 0.070, 0.080), "av_plate_dk", "hand_" + side)
        r.add("cuff", TORUS(0.070, 0.018), "av_scale_lt", "hand_" + side, loc=(0, 0, 0.065))

    # The helm: a tall bucket battered out of true, a ridge of scale spines over
    # the crown, the visor slit dark, and the faceplate split across it with
    # the ember showing through the crack and a piece of it gone.
    r.joint("neck", (0, 0, 0.53), "chest")
    r.joint("head", (0, 0, 0.05), "neck", rest=(-6, 0, 0))
    # The top is the helm's own dark, not a pale cap: pale, it read from above
    # as a bald head in a headband.
    r.add("helm", C(0.138, 0.142, 0.19), "av_plate", "head", loc=(0, 0.0, 0.28))
    r.add("helm_top", E(0.130, 0.130, 0.036), "av_plate_dk", "head", loc=(0, 0.0, 0.405))
    for k in range(4):
        r.add("spine", E(0.024, 0.044, 0.054 - k * 0.006), "av_scale_lt", "head",
              loc=(0, -0.07 + k * 0.060, 0.440 - k * 0.012))
    r.add("ridge", E(0.020, 0.016, 0.110), "av_plate_lt", "head", loc=(0, -0.140, 0.25))
    r.add("visor", E(0.116, 0.026, 0.026), "av_void", "head", loc=(0, -0.132, 0.272))
    r.add("eye_glow", E(0.032, 0.012, 0.015), "av_ember_glow", "head", loc=(-0.040, -0.142, 0.272))
    # The split: down from the slit across the cheek, and the piece knocked out.
    r.add("split", E(0.013, 0.010, 0.075), "av_ember_glow", "head", loc=(0.050, -0.138, 0.205), rot=(0, 0.45, 0))
    r.add("gone", E(0.044, 0.020, 0.038), "av_void", "head", loc=(0.080, -0.128, 0.150))
    for sx in (-1, 1):
        r.add("breath", E(0.013, 0.010, 0.013), "av_void", "head", loc=(sx * 0.060, -0.132, 0.170))

    # Ash drifting up out of the neck.
    r.joint("ash", (0, 0, 0.60), "chest")
    for k, (x, y, z, rr) in enumerate(((0.02, -0.02, 0.10, 0.022), (-0.06, 0.04, 0.24, 0.018),
                                       (0.07, 0.02, 0.36, 0.016))):
        r.add("fleck", E(rr, rr, rr), ("av_ash", "av_ash_dk", "av_ash")[k], "ash", loc=(x, y, z))

    # The greatsword, along the right hand's -Z: a long grip, a heavy guard
    # flared at the ends, and a broad blade gone grey and notched.
    r.joint("sword", (0, 0, 0), "hand_r")
    _greatsword(r, "sword")

    # What is left: a heap of ash with plates in it, and the sword lying by
    # it -- kept at nothing until the death needs them.
    r.joint("heap", (0, 0, 0), "pose")
    for k, (x, y, z, rx, rz, col) in enumerate((
            (0.00, 0.00, 0.04, 0.34, 0.10, "av_ash_dk"), (-0.12, -0.04, 0.09, 0.20, 0.10, "av_ash"),
            (0.10, 0.03, 0.10, 0.22, 0.11, "av_ash"), (0.00, -0.10, 0.13, 0.16, 0.10, "av_ash"),
            (0.02, 0.06, 0.18, 0.12, 0.08, "av_ash"))):
        r.add("ash_heap", E(rx, rx * 0.8, rz), col, "heap", loc=(x, y, z))
    r.add("heap_plate", E(0.12, 0.09, 0.05), "av_plate", "heap", loc=(-0.10, -0.12, 0.14), rot=(0.5, 0.3, 0))
    r.add("heap_helm", C(0.09, 0.095, 0.11), "av_plate", "heap", loc=(0.14, -0.06, 0.12), rot=(1.2, 0.2, 0.4))
    r.add("heap_scale", E(0.07, 0.05, 0.03), "av_scale", "heap", loc=(0.02, -0.16, 0.20), rot=(0.4, 0, 0))
    r.add("heap_ember", E(0.035, 0.030, 0.020), "av_ember_glow", "heap", loc=(-0.02, -0.06, 0.20))
    # Laid flat: the blade turned from hanging to lying along the ground,
    # then round to lie across in front of the heap.
    r.joint("dropped", (-0.44, -0.06, 0.035), "pose", rest=(-90, 0, 70))
    _greatsword(r, "dropped")
    # Measured without the sword, whose point hangs out in front of the feet.
    return fitted(r, "ashen_vanguard", 60, dict(AV_REST, **{"~sword": -0.99}))


def _greatsword(r, parent):
    r.limb("grip", (0, 0, 0.16), (0, 0, -0.04), 0.024, "av_grip", parent, r_tip=0.024)
    r.add("pommel", E(0.045, 0.045, 0.045), "av_plate_lt", parent, loc=(0, 0, 0.19))
    r.add("guard", E(0.22, 0.040, 0.035), "av_plate_dk", parent, loc=(0, 0, -0.06))
    for sx in (-1, 1):
        r.add("quillon", E(0.040, 0.045, 0.040), "av_scale_lt", parent, loc=(sx * 0.21, 0, -0.04))
    r.add("blade", C(0.068, 0.030, 0.94, squash_y=0.28), "av_blade", parent, loc=(0, 0, -0.07))
    r.add("fuller", C(0.018, 0.010, 0.70, squash_y=0.50), "av_blade_dk", parent, loc=(0, -0.014, -0.10))
    r.add("scorch", E(0.050, 0.012, 0.10), "av_blade_dk", parent, loc=(0.01, -0.016, -0.62))
    for k, (sx, z) in enumerate(((1, -0.30), (-1, -0.52), (1, -0.74))):
        r.add("notch", E(0.022, 0.016, 0.026), "av_void", parent, loc=(sx * 0.062, 0, z))


def _av(hold, aim, gap=0.11, **more):
    v = dict(AV_HIDDEN)
    v.update(pr._grip(hold, aim, gap=gap))
    v.update(more)
    return v


# The heap and the dropped sword are kept at nothing until the death.
AV_HIDDEN = {"~heap": -0.99, "~dropped": -0.99}

# On guard, low: both hands on the hilt at the belt, the blade angled down and
# across in front, the point near the ground.
AV_REST = _av((0.05, -0.27, 0.08), (-0.42, -0.55, -0.72), hip_l=fwd(12), hip_r=fwd(-8), knee_l=X(14),
              knee_r=X(10), _z=-0.02)


def av_idle(t):
    # Heavy breathing: the shoulders heave and settle, the helm turns a
    # little, the cloak stirs, and ash rises out of the neck.
    s = sn(t)
    v = dict(AV_REST)
    v.update(chest=X(9 + 3 * s), neck=X(-2 * s), head=(0, 0, 4 * sn(t, 0.25)), cloak=X(4 * sn(t, 0.3)),
             pauldron_l=(0, 0, 3 * s), pauldron_r=(0, 0, -3 * s), _z=-0.02 + 0.010 * s)
    v["@ash"] = (0.0, 0.0, 0.12 * t)
    return v


def av_walk(t):
    # A lurching, uneven stride -- a man who has been hurt, and does not care
    # -- the greatsword dragged point-first behind it in the right hand, the
    # left swinging, the body rolling over each step.
    s = sn(t)
    v = dict(AV_HIDDEN)
    v.update(cr.gait(t, 26, 26, 0, 0.034, 12))
    v["hip_r"] = fwd(-18 * s)          # the bad leg: a shorter swing
    # Down and out to the side as much as behind: straight behind, it stood up
    # over the shoulder in the row that faces the camera.
    v.update({"^r": (0.27, -0.06, -0.10), "^r_aim": (0.52, 0.46, -0.72),
              "shoulder_l": fwd(-26 * s), "elbow_l": X(-30),
              "pelvis": (0, 7 * s, 0), "chest": (12, -5 * s, -6 * s), "head": (0, 0, 5 * s),
              "cloak": (10 + 6 * sn(t, 0.25), 0, 0)})
    v["@ash"] = (0.0, 0.10, 0.10 * t)
    return v


def av_run(t):
    # The lurching sprint: bent right over, long uneven strides, the blade held
    # low and trailing in the right hand, the left arm thrown forward, and the
    # cloak streaming out behind.
    s, c = sn(t), sn(t, 0.25)
    v = dict(AV_HIDDEN)
    v.update({"hip_l": fwd(44 * s + 6), "hip_r": fwd(-36 * s + 6),
              "knee_l": X(10 + 60 * max(0.0, -s)), "knee_r": X(10 + 54 * max(0.0, s)),
              "^r": (0.30, 0.02, -0.12), "^r_aim": (0.50, 0.50, -0.71),
              "shoulder_l": fwd(30 - 40 * s), "elbow_l": X(-50),
              "pelvis": (0, 6 * s, 4 * s), "chest": (28, -4 * s, -8 * s), "neck": X(-14), "head": (0, 0, 4 * s),
              "cloak": (-34 + 8 * c, 0, 4 * s), "_z": 0.05 * abs(c) + 0.01, "_y": -0.02})
    v["@ash"] = (0.0, 0.16, 0.06)
    return v


# The slash, frame by frame: drawn back over the right shoulder, brought down
# and across to the left with the whole body turning into it, and back.
AV_SLASH = [
    AV_REST,
    _av((0.16, -0.12, 0.38), (0.32, 0.30, 0.90), chest=(2, 0, 16), hip_l=fwd(6), hip_r=fwd(-12), knee_l=X(14),
        knee_r=X(12)),
    _av((0.12, -0.20, 0.48), (0.36, 0.48, 0.80), chest=(-2, 0, 22), head=(0, 0, -8), hip_l=fwd(6),
        hip_r=fwd(-14), knee_l=X(16), knee_r=X(12)),
    _av((-0.04, -0.34, 0.20), (-0.40, -0.86, -0.32), chest=(20, 0, -18), head=(0, 0, 10), hip_l=fwd(28),
        hip_r=fwd(-20), knee_l=X(24), knee_r=X(8), _y=-0.10),
    _av((-0.10, -0.28, 0.04), (-0.62, -0.50, -0.60), chest=(22, 0, -24), head=(0, 0, 12), hip_l=fwd(30),
        hip_r=fwd(-22), knee_l=X(26), knee_r=X(8), _y=-0.11),
    _av((0.02, -0.28, 0.08), (-0.46, -0.55, -0.70), chest=(14, 0, -8), hip_l=fwd(18), hip_r=fwd(-12),
        knee_l=X(18), knee_r=X(10), _y=-0.05),
]


def av_attack(t):
    return dict(AV_SLASH[min(5, int(round(t * 5)))])


# The sunder: lifted in front, raised right back over the helm with the body
# arched under it, a beat at the top, and brought down two-handed into the
# ground in front -- the blade lands on the fifth frame and is buried on the
# sixth.
AV_SUNDER = [
    AV_REST,
    _av((0.0, -0.30, 0.34), (0.0, -0.30, 0.95), chest=X(0), knee_l=X(12), knee_r=X(10)),
    _av((0.0, -0.04, 0.76), (0.0, 0.40, 0.92), chest=X(-12), neck=X(-6), head=X(-6), hip_l=fwd(8),
        hip_r=fwd(-10), knee_l=X(14), knee_r=X(12), _z=0.02),
    _av((0.0, -0.02, 0.80), (0.0, 0.52, 0.85), chest=X(-15), neck=X(-8), head=X(-8), hip_l=fwd(8),
        hip_r=fwd(-10), knee_l=X(12), knee_r=X(10), _z=0.04),
    _av((0.0, -0.40, 0.14), (0.0, -0.62, -0.78), chest=X(30), head=X(8), hip_l=fwd(34), hip_r=fwd(-22),
        knee_l=X(36), knee_r=X(14), _y=-0.14, _z=-0.06),
    _av((0.0, -0.36, -0.02), (0.0, -0.48, -0.88), chest=X(36), head=X(10), hip_l=fwd(38), hip_r=fwd(-24),
        knee_l=X(44), knee_r=X(18), _y=-0.15, _z=-0.09),
]


def av_heavy(t):
    return dict(AV_SUNDER[min(5, int(round(t * 5)))])


def av_hurt(t):
    # A stagger: knocked back a step, the helm jolted, the pauldrons thrown
    # about, and the ash puffing out of the neck.
    k = math.sin(t * math.pi)
    v = dict(AV_REST)
    v.update({"chest": X(9 - 20 * k), "_y": 0.08 * k, "head": (14 * k, 0, 18 * k), "@head": (0, 0, 0.04 * k),
              "@pauldron_l": (-0.02 * k, 0, 0.03 * k), "@pauldron_r": (0.025 * k, 0, -0.02 * k),
              "knee_l": X(14 + 12 * k), "pauldron_l": (0, 0, 14 * k), "pauldron_r": (0, 0, -12 * k),
              "cloak": X(-16 * k), "~ash": 0.6 * k, "@ash": (0, 0.04 * k, 0.10 * k)})
    return v


def av_death(t):
    """Down onto one knee, the greatsword planted and then slipping out of its
    hands, and the whole of it crumbling into a heap of ash with a plate or
    two showing -- the sword lying in the cinders beside it."""
    frame = min(5, int(round(t * 5)))
    kneel = dict(hip_l=fwd(70), knee_l=X(78), hip_r=fwd(-6), knee_r=X(96))
    if frame == 0:
        v = dict(AV_REST)
        v.update(chest=X(-12), neck=X(-10), head=(-10, 0, 14), _y=0.05, knee_l=X(22), knee_r=X(16))
        return v
    if frame == 1:
        return _av((0.02, -0.32, 0.06), (0.0, -0.22, -0.97), gap=0.12, chest=X(14), head=X(10), _z=-0.20,
                   hip_l=fwd(48), knee_l=X(60), hip_r=fwd(-4), knee_r=X(66))
    if frame == 2:
        return _av((0.02, -0.34, 0.00), (0.0, -0.16, -0.99), gap=0.12, chest=X(22), neck=X(12), head=X(16),
                   _z=-0.30, **kneel)
    # From here the sword is out of its hands and lying in the ash, and the
    # harness is going down into it.
    body, heap = ((0.86, 0.45), (0.45, 0.85), (0.0, 1.0))[frame - 3]
    v = dict(_av((0.10, -0.30, -0.06), (0.4, -0.3, -0.85), gap=0.12, chest=X(32), neck=X(16), head=(24, 0, 10),
                 _z=-0.30, **kneel))
    v["~sword"] = -0.99
    v["~dropped"] = 0.0
    v["~heap"] = -0.99 + 0.99 * heap
    v["%base"] = (1.0 + 0.15 * (1.0 - body), 1.0 + 0.15 * (1.0 - body), max(0.01, body * body))
    if body <= 0.0:
        v["~base"] = -0.99
        v.pop("%base")
    return v


# =================================================================================
#  The Forge Demon
# =================================================================================
def _dent(r, parent, loc, size=0.07):
    """A hammer dent: a dark dish in the plate."""
    r.add("dent", E(size, size * 0.35, size * 0.8), "fd_dent", parent, loc=loc)


def build_forge_demon():
    """Halda's nightmare: a hulking giant built of heavy blocks of blackened
    plate, and between every plate the molten metal it is made of showing
    through the seams. Its helm is an anvil, laid on its side, the horn out
    over one shoulder and the worn steel face on top; its armour is dented
    where a hammer has rung off it, and on its shoulders and back are cooling
    vents. It drags a sword as long as a wagon. Wide, short in the leg and
    huge in the shoulder, so it is slow before it moves."""
    r = Rig()
    r.joint("base", (0, 0, 0))                       # what the whirlwind turns
    r.joint("pelvis", (0, 0, 1.04), "base")
    r.add("hip_core", E(0.46, 0.34, 0.18), "fd_glow", "pelvis", loc=(0, 0, 0.02))
    r.add("hips", rbox(0.98, 0.66, 0.22, 0.25), "fd_iron_dk", "pelvis", loc=(0, 0, -0.02))
    for k in range(8):
        a = math.radians(-90 + k * 45)
        r.add("tasset", rbox(0.32, 0.10, 0.30, 0.3), ("fd_iron", "fd_iron_dk")[k % 2], "pelvis",
              loc=(math.cos(a) * 0.46, math.sin(a) * 0.36, -0.20), rot=(math.radians(-10), 0, a + math.pi / 2))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.33, 0, -0.06), "pelvis", rest=(0, sx * -5, 0))
        r.add("thigh_core", E(0.15, 0.15, 0.20), "fd_glow", "hip_" + side, loc=(0, 0, -0.22))
        r.add("thigh", rbox(0.36, 0.36, 0.36, 0.3), "fd_iron", "hip_" + side, loc=(0, 0, -0.21))
        r.joint("knee_" + side, (0, 0, -0.46), "hip_" + side)
        r.add("knee_glow", E(0.17, 0.15, 0.09), "fd_glow", "knee_" + side, loc=(0, 0, 0.0))
        r.add("poleyn", rbox(0.30, 0.20, 0.17, 0.3), "fd_iron_lt", "knee_" + side, loc=(0, -0.12, 0.02))
        r.add("greave", rbox(0.32, 0.32, 0.36, 0.3), "fd_iron", "knee_" + side, loc=(0, 0, -0.22))
        r.add("sabaton", rbox(0.38, 0.54, 0.17, 0.3), "fd_iron_dk", "knee_" + side, loc=(0, -0.08, -0.44))
        _dent(r, "hip_" + side, (sx * 0.06, -0.18, -0.16), 0.06)

    # The body: the molten core, and slabs of plate laid over it, each forged
    # a little out of true, with a seam of light between each. Hunched well
    # forward over its belly: upright it was a machine.
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(12, 0, 0))
    r.joint("core", (0, 0, 0.64), "chest")
    r.add("core", E(0.60, 0.42, 0.64), "fd_glow", "core")
    for sx in (-1, 1):
        # The breast, either side of a seam down the middle that burns.
        r.add("pec", rbox(0.56, 0.24, 0.46, 0.45), "fd_iron", "chest", loc=(sx * 0.31, -0.33, 0.97),
              rot=(math.radians(-8), sx * math.radians(6), sx * math.radians(-12)))
        r.add("abdomen", rbox(0.28, 0.24, 0.42, 0.45), "fd_iron_dk", "chest", loc=(sx * 0.42, -0.26, 0.40),
              rot=(0, sx * math.radians(-6), sx * math.radians(-20)))
        # Broad at the top and drawn in to the waist: a brute's wedge.
        r.add("flank", rbox(0.22, 0.66, 0.70, 0.45), "fd_iron", "chest", loc=(sx * 0.60, 0.02, 0.78),
              rot=(0, sx * math.radians(-10), 0))
        r.add("waist_side", rbox(0.20, 0.56, 0.40, 0.45), "fd_iron_dk", "chest", loc=(sx * 0.46, 0.02, 0.24))
        r.add("back", rbox(0.54, 0.24, 0.84, 0.45), "fd_iron", "chest", loc=(sx * 0.31, 0.38, 0.74),
              rot=(math.radians(6), 0, sx * math.radians(10)))
        _dent(r, "chest", (sx * 0.24, -0.46, 1.05), 0.07)
        # The seam under the breast, bright.
        r.add("seam", rbox(0.46, 0.06, 0.05, 0.3), "fd_glow", "chest", loc=(sx * 0.26, -0.36, 0.70),
              rot=(0, sx * math.radians(-8), 0))
    r.add("sternum", rbox(0.07, 0.07, 0.58, 0.3), "fd_glow", "chest", loc=(0, -0.42, 0.98))
    _dent(r, "chest", (0.40, -0.45, 0.86), 0.05)
    # The furnace in its belly: a grate of bars over the fire, which the
    # breath of flame turns up.
    r.joint("belly", (0, -0.30, 0.38), "chest")
    r.add("furnace", E(0.28, 0.14, 0.23), "fd_coal", "belly", loc=(0, 0.02, 0.0))
    r.add("fire", E(0.26, 0.12, 0.21), "fd_hot_glow", "belly", loc=(0, -0.01, 0.0))
    r.add("grate_frame", rbox(0.62, 0.12, 0.50, 0.25), "fd_iron_dk", "chest", loc=(0, -0.34, 0.38))
    r.add("grate_hole", rbox(0.46, 0.14, 0.36, 0.1), "fd_coal", "chest", loc=(0, -0.345, 0.38))
    for k in range(4):
        r.add("bar", rbox(0.050, 0.10, 0.42, 0.2), "fd_iron", "chest", loc=(-0.165 + k * 0.11, -0.41, 0.38))
    # Cooling vents down the back between the back plates, glowing between
    # their fins; the steam comes out of these when it is hurt.
    r.add("vent_glow", rbox(0.26, 0.06, 0.52, 0.3), "fd_glow", "chest", loc=(0, 0.45, 0.88))
    for k in range(5):
        r.add("back_vent", rbox(0.34, 0.10, 0.05, 0.2), "fd_iron_dk", "chest", loc=(0, 0.48, 1.08 - k * 0.11))
    r.add("collar", rbox(0.78, 0.66, 0.18, 0.45), "fd_iron_dk", "chest", loc=(0, 0.06, 1.22))
    # The small of the back, or the core shows there as a glowing seat.
    r.add("loin", rbox(0.84, 0.26, 0.36, 0.45), "fd_iron_dk", "chest", loc=(0, 0.33, 0.20))

    # Pauldrons: two great rounded slabs a side, sloping off the shoulder,
    # the heat showing between them.
    for sx, side in ((-1, "l"), (1, "r")):
        p = "pauldron_" + side
        # Smaller and sloped well down off the shoulder: level and as wide as
        # the arms, the pair of them made one bar across the top of it.
        r.joint(p, (sx * 0.74, 0.02, 1.14), "chest", rest=(0, sx * -28, 0))
        r.add("pauldron_core", E(0.28, 0.30, 0.14), "fd_glow", p, loc=(sx * 0.06, 0, -0.08))
        r.add("lame_top", rbox(0.54, 0.64, 0.24, 0.55), "fd_iron", p, loc=(sx * 0.02, 0, 0.10))
        r.add("lame_low", rbox(0.50, 0.60, 0.20, 0.55), "fd_iron_dk", p, loc=(sx * 0.12, 0, -0.13))
        r.add("rim", rbox(0.09, 0.62, 0.08, 0.4), "fd_iron_lt", p, loc=(sx * 0.30, 0, 0.12))
        _dent(r, p, (sx * 0.16, -0.37, 0.10), 0.06)
        # Steam out of the back's vents, kept at nothing until it is hurt.
        r.joint("steam_" + side, (-sx * 0.40, 0.42, 0.10), p)
        for k, (x, y, z, rr) in enumerate(((0.0, 0.0, 0.10, 0.17), (sx * 0.12, 0.06, 0.32, 0.21),
                                           (-sx * 0.06, -0.02, 0.54, 0.18), (sx * 0.04, 0.08, 0.74, 0.14))):
            r.add("steam", E(rr, rr, rr * 0.85), ("fd_steam", "fd_steam_dk")[k % 2], "steam_" + side, loc=(x, y, z))

    # Arms like a smith's, too long and too thick, ending in great blocks of
    # gauntlet.
    for sx, side in ((-1, "l"), (1, "r")):
        sh = "shoulder_" + side
        r.joint(sh, (sx * 0.78, 0, 0.98), "chest", rest=(-8, sx * -14, 0))
        r.add("upper", rbox(0.36, 0.36, 0.52, 0.45), "fd_iron", sh, loc=(0, 0, -0.24))
        r.joint("elbow_" + side, (0, 0, -0.52), sh, rest=(-26, 0, 0))
        r.add("elbow_glow", E(0.18, 0.17, 0.09), "fd_glow", "elbow_" + side, loc=(0, 0, 0.02))
        r.add("couter", rbox(0.26, 0.20, 0.18, 0.4), "fd_iron_lt", "elbow_" + side, loc=(0, 0.14, 0.0))
        r.add("vambrace", rbox(0.44, 0.44, 0.52, 0.45), "fd_iron_dk", "elbow_" + side, loc=(0, 0, -0.29))
        r.joint("hand_" + side, (0, 0, -0.60), "elbow_" + side)
        r.add("wrist_glow", E(0.17, 0.17, 0.07), "fd_glow", "hand_" + side, loc=(0, 0, 0.03))
        r.add("fist", rbox(0.40, 0.34, 0.34, 0.45), "fd_iron", "hand_" + side, loc=(0, 0, -0.13))
        _dent(r, "elbow_" + side, (0, -0.22, -0.30), 0.06)

    # The helm: an anvil laid on its side, thrust forward low between the
    # shoulders -- a collar for a foot, a dark waist with the eyes burning in
    # it, the worn steel face on top, the horn sweeping out and up over one
    # shoulder like a demon's, and the heel to the other side with its
    # hardy hole.
    # Raised enough that the anvil's waist and horn clear the shoulders.
    r.joint("neck", (0, -0.22, 1.20), "chest", rest=(-10, 0, 0))
    r.joint("head", (0, -0.04, 0.06), "neck", rest=(-6, 0, 0))
    # Seen from in front it is an anvil's profile: a flared foot, a step, a
    # narrow waist, and the long face on top running out to the horn on one
    # side and the square heel on the other. The face only a shade lighter:
    # pale, it was a box worn as a hat.
    r.add("helm_foot", rbox(0.64, 0.50, 0.12, 0.35), "fd_iron_dk", "head", loc=(0, 0, 0.06))
    r.add("helm_step", rbox(0.48, 0.40, 0.08, 0.3), "fd_iron", "head", loc=(0, 0, 0.15))
    r.add("helm_waist", rbox(0.34, 0.32, 0.20, 0.3), "fd_iron_dk", "head", loc=(0, 0, 0.27))
    for sx in (-1, 1):
        r.add("eye", rbox(0.10, 0.05, 0.07, 0.3), "fd_hot_glow", "head", loc=(sx * 0.085, -0.165, 0.27),
              rot=(0, sx * math.radians(-12), 0))
    r.add("helm_face", rbox(0.78, 0.40, 0.15, 0.2), "fd_iron", "head", loc=(0.06, 0, 0.43))
    r.add("helm_top", rbox(0.74, 0.36, 0.03, 0.15), "fd_iron_lt", "head", loc=(0.06, 0, 0.51))
    r.limb("horn", (-0.30, 0, 0.43), (-0.60, -0.01, 0.47), 0.115, "fd_iron", "head", r_tip=0.070)
    r.limb("horn_tip", (-0.60, -0.01, 0.47), (-0.76, -0.02, 0.60), 0.070, "fd_iron_lt", "head", r_tip=0.012)
    r.add("hardy", rbox(0.08, 0.08, 0.03, 0.1), "fd_dent", "head", loc=(0.30, 0.04, 0.525))
    _dent(r, "head", (-0.10, -0.21, 0.43), 0.05)

    # The sword, along the right hand's -Z: a grip for a fist that size, a bar
    # of a guard, and a broad slab of rough iron with the heat still in a
    # crack down its middle.
    r.joint("sword", (0, 0, -0.10), "hand_r")
    _demon_sword(r, "sword")
    # The same sword lying where it fell, kept at nothing until the death.
    r.joint("dropped", (1.05, -0.55, 0.03), "pose", rest=(-90, 0, 40))
    _demon_sword(r, "dropped")
    # Measured without the sword: its point on the ground in front stands
    # out below the feet, and fitted with it the body came out a fifth small.
    return fitted(r, "forge_demon", 112, dict(FD_REST, **{"~sword": -0.99}))


def _demon_sword(r, parent):
    """A sword as long as a wagon -- about seventy pixels, three of the player
    laid end to end: a grip for a fist that size, a bar of a guard, and a
    broad slab of rough iron with the heat still in a crack down its middle.
    Twice that, it could not be swung inside the frame."""
    r.limb("grip", (0, 0, 0.22), (0, 0, -0.05), 0.052, "fd_grip", parent, r_tip=0.052)
    r.add("pommel", rbox(0.13, 0.13, 0.11, 0.3), "fd_iron_dk", parent, loc=(0, 0, 0.25))
    r.add("guard", rbox(0.50, 0.14, 0.11, 0.3), "fd_iron_dk", parent, loc=(0, 0, -0.10))
    r.add("blade", rbox(0.28, 0.07, 0.92, 0.12), "fd_blade", parent, loc=(0, 0, -0.56))
    r.add("edge", rbox(0.05, 0.065, 0.86, 0.2), "fd_blade_lt", parent, loc=(0.13, 0, -0.58))
    r.add("crack", rbox(0.045, 0.08, 0.66, 0.3), "fd_glow", parent, loc=(-0.02, 0, -0.52))
    for z in (-0.35, -0.65, -0.88):
        r.add("notch", rbox(0.06, 0.09, 0.06, 0.2), "fd_dent", parent, loc=(0.14, 0, z))
    r.add("tip", rbox(0.22, 0.07, 0.13, 0.3), "fd_blade", parent, loc=(-0.03, 0, -1.04),
          rot=(0, math.radians(-24), 0))


FD_HIDDEN = {"~steam_l": -0.99, "~steam_r": -0.99, "~belly": 0.0, "~dropped": -0.99}

# Standing: the left fist hanging, the sword in the right with its point on
# the ground out to the side and in front.
# The point is kept within a couple of units of the feet in every direction:
# further out towards the camera, the side row's frame could not hold it and
# the whole demon was lifted to make room.
FD_REST = dict(FD_HIDDEN, **{"^r": (0.92, -0.26, -0.02), "^r_aim": (0.45, -0.35, -0.82), "shoulder_l": fwd(10),
                             "elbow_l": X(-22), "hip_l": (0, -4, 0), "hip_r": (0, 4, 0)})


def _fd(**more):
    v = dict(FD_REST)
    v.update(more)
    return v


def fd_idle(t):
    # It breathes like a bellows: the shoulders lift and drop, the molten core
    # swells and settles, the head turns a little.
    s = sn(t)
    return _fd(chest=X(8 + 2.5 * s), neck=X(-1.5 * s), head=(0, 0, 4 * sn(t, 0.25)), _z=0.012 * s,
               pauldron_l=(0, 0, 2 * s), pauldron_r=(0, 0, -2 * s), shoulder_l=(-10 - 3 * s, 0, 0),
               **{"~core": 0.025 * s, "~belly": 0.06 * s})


def fd_walk(t):
    # Lumbering: short heavy steps from a wide stance, the whole weight
    # rolling from foot to foot, and the sword dragged along the ground at
    # its side.
    s, c = sn(t), sn(t, 0.25)
    return _fd(**{"hip_l": (-20 * s, -4, 0), "hip_r": (20 * s, 4, 0),
                  "knee_l": X(26 * max(0.0, -s)), "knee_r": X(26 * max(0.0, s)),
                  "pelvis": (0, 6 * s, 0), "chest": (12, -4 * s, -5 * s), "head": (0, 0, 4 * s),
                  "shoulder_l": fwd(-18 * s + 4), "elbow_l": X(-26),
                  "^r": (0.94, -0.10, -0.06), "^r_aim": (0.50, 0.40, -0.77),
                  "_z": 0.035 * abs(c), "~core": 0.02 * c})


# The smash: the sword hauled up over the right shoulder and brought down in
# front, one-handed, with the body coming over after it.
FD_SMASH = [
    FD_REST,
    _fd(**{"^r": (0.70, -0.20, 0.70), "^r_aim": (0.30, -0.05, 0.95), "chest": (2, 0, 12), "shoulder_l": fwd(20),
           "elbow_l": X(-40)}),
    _fd(**{"^r": (0.50, 0.00, 1.20), "^r_aim": (0.20, 0.55, 0.81), "chest": (-6, 0, 16), "head": X(-6),
           "shoulder_l": fwd(30), "elbow_l": X(-50), "_z": 0.02}),
    # Down and across, not straight at the camera, where a blade is a point.
    _fd(**{"^r": (0.24, -0.60, 0.32), "^r_aim": (-0.45, -0.45, -0.77), "chest": (22, 0, -8), "head": X(6),
           "hip_l": fwd(16), "hip_r": fwd(-10), "knee_l": X(20), "_y": -0.10}),
    _fd(**{"^r": (0.20, -0.60, 0.06), "^r_aim": (-0.50, -0.34, -0.80), "chest": (28, 0, -10), "head": X(8),
           "hip_l": fwd(20), "hip_r": fwd(-12), "knee_l": X(24), "knee_r": X(8), "_y": -0.12, "_z": -0.04}),
    _fd(**{"^r": (0.60, -0.55, 0.00), "^r_aim": (0.30, -0.70, -0.65), "chest": (16, 0, -2), "_y": -0.05}),
]


def fd_attack(t):
    return dict(FD_SMASH[min(5, int(round(t * 5)))])


# The wide sweep: wound back to the right with the body turned away, then the
# whole blade brought round level in front and on round to the left.
FD_SWEEP = [
    FD_REST,
    _fd(**{"^r": (1.00, 0.30, 0.20), "^r_aim": (0.70, 0.70, -0.12), "chest": (6, 0, -30), "head": (0, 0, 14),
           "shoulder_l": (-40, -20, 0), "elbow_l": X(-40), "hip_l": fwd(10), "hip_r": fwd(-10)}),
    _fd(**{"^r": (1.02, 0.36, 0.26), "^r_aim": (0.50, 0.86, -0.08), "chest": (6, 0, -40), "head": (0, 0, 18),
           "shoulder_l": (-50, -24, 0), "elbow_l": X(-40), "hip_l": fwd(12), "hip_r": fwd(-12), "_z": 0.01}),
    _fd(**{"^r": (0.70, -0.70, 0.30), "^r_aim": (0.20, -0.98, -0.06), "chest": (10, 0, 6), "head": (0, 0, 0),
           "shoulder_l": fwd(-10), "elbow_l": X(-30), "hip_l": fwd(16), "hip_r": fwd(-12), "knee_l": X(18)}),
    _fd(**{"^r": (-0.20, -0.86, 0.30), "^r_aim": (-0.80, -0.58, -0.10), "chest": (12, 0, 34), "head": (0, 0, -14),
           "shoulder_l": fwd(-30), "elbow_l": X(-30), "hip_l": fwd(18), "hip_r": fwd(-14), "knee_l": X(20),
           "_y": -0.06}),
    _fd(**{"^r": (0.20, -0.70, 0.10), "^r_aim": (-0.55, -0.70, -0.45), "chest": (12, 0, 20), "head": (0, 0, -8),
           "hip_l": fwd(10), "hip_r": fwd(-8), "_y": -0.03}),
]


def fd_heavy(t):
    return dict(FD_SWEEP[min(5, int(round(t * 5)))])


# The breath: it straightens and draws itself up, the furnace in its belly
# roaring brighter behind the grate, and leans out over its feet with its
# arms thrown back to let it go -- the flame itself is the game's.
FD_FLAME = [
    (FD_REST, 0.0),
    (_fd(chest=X(4), neck=X(-4), shoulder_l=(-10, -24, 0), elbow_l=X(-30)), 0.12),
    (_fd(chest=X(-8), neck=X(-10), head=X(-8), shoulder_l=(10, -36, 0), elbow_l=X(-40),
         **{"^r": (1.02, 0.10, 0.10), "^r_aim": (0.60, 0.40, -0.69), "_z": 0.02}), 0.30),
    # Thrown forward from the knees with the body held upright and the belly
    # out in front of it: leant over from the waist, the furnace went under
    # the chest and the camera never saw it.
    (_fd(chest=X(-2), neck=X(-6), head=X(-4), shoulder_l=(24, -50, 0), elbow_l=X(-20),
         hip_l=fwd(20), hip_r=fwd(-6), knee_l=X(26), knee_r=X(16),
         **{"^r": (1.06, 0.16, 0.02), "^r_aim": (0.60, 0.40, -0.69), "_y": -0.12, "_z": -0.05}), 0.62),
    (_fd(chest=X(-4), neck=X(-8), head=X(-6), shoulder_l=(30, -54, 0), elbow_l=X(-16),
         hip_l=fwd(24), hip_r=fwd(-8), knee_l=X(30), knee_r=X(18),
         **{"^r": (1.06, 0.20, 0.00), "^r_aim": (0.60, 0.40, -0.69), "_y": -0.16, "_z": -0.07}), 0.80),
    (_fd(chest=X(-4), neck=X(-9), head=X(-6), shoulder_l=(32, -54, 0), elbow_l=X(-16),
         hip_l=fwd(24), hip_r=fwd(-8), knee_l=X(30), knee_r=X(18),
         **{"^r": (1.06, 0.20, 0.00), "^r_aim": (0.60, 0.40, -0.69), "_y": -0.17, "_z": -0.07}), 0.90),
]


def fd_flame(t):
    pose, fire = FD_FLAME[min(5, int(round(t * 5)))]
    v = dict(pose)
    v["~belly"] = fire
    v["~core"] = 0.04 * max(0.0, fire)
    return v


def fd_spin(t):
    """The whirlwind: planted low, the sword thrown out at arm's length and
    level, the other arm out against it, and the whole of it turning on the
    spot -- a full turn every eight frames, so it loops."""
    v = _fd(**{"^r": (1.02, -0.30, 0.40), "^r_aim": (0.80, -0.40, -0.45), "chest": (14, 0, 0),
               "shoulder_l": (-24, -60, 0), "elbow_l": X(-20), "hip_l": (-10, -10, 0), "hip_r": (10, 10, 0),
               "knee_l": X(24), "knee_r": X(24), "_z": -0.08, "head": X(6)})
    v["base"] = (0, 0, -360.0 * t)
    return v


def fd_hurt(t):
    # Rocked back on its heels, the helm knocked up, and steam blasting out
    # of the shoulder vents.
    k = math.sin(t * math.pi)
    return _fd(chest=X(8 - 16 * k), head=(-10 * k, 0, 12 * k), neck=X(-6 * k), _y=0.06 * k,
               pauldron_l=(0, 0, 6 * k), pauldron_r=(0, 0, -6 * k),
               **{"~steam_l": -0.99 + 0.99 * min(1.0, 0.3 + k), "~steam_r": -0.99 + 0.99 * min(1.0, 0.3 + k),
                  "~core": 0.06 * k})


def fd_death(t):
    """Down onto one knee with the fire going out of it, the sword dropping
    out of its hand, then over onto its side with a crash, and apart -- the
    pauldrons and the helm off it and the seams gone dull. It goes over on
    the joint at its feet rather than the world's, so the sword it dropped
    stays lying flat where it fell."""
    frame = min(5, int(round(t * 5)))
    kneel = {"hip_l": fwd(70), "knee_l": X(80), "hip_r": fwd(-4), "knee_r": X(96)}
    if frame == 0:
        return _fd(chest=X(-8), head=(-12, 0, 10), _y=0.05, **{"~core": 0.05})
    if frame == 1:
        return _fd(chest=X(18), head=X(10), _z=-0.30, **dict(kneel, **{"^r": (0.80, -0.62, 0.10),
                                                                       "^r_aim": (0.10, -0.30, -0.95),
                                                                       "~core": -0.12, "~belly": -0.40}))
    if frame == 2:
        return _fd(chest=X(30), neck=X(10), head=X(14), _z=-0.34, shoulder_l=fwd(30), elbow_l=X(-40),
                   **dict(kneel, **{"^r": (0.86, -0.58, 0.00), "^r_aim": (0.30, -0.25, -0.92),
                                    "~core": -0.30, "~belly": -0.60}))
    over = (0.55, 1.0, 1.0)[frame - 3]
    roll = 84.0 * over
    v = _fd(chest=X(24), neck=X(10), head=X(10), shoulder_l=fwd(40), elbow_l=X(-30),
            shoulder_r=fwd(30), elbow_r=X(-20), **dict(kneel, **{"~core": -0.45 - 0.15 * over,
                                                                  "~belly": -0.80}))
    v.pop("^r")
    v.pop("^r_aim")
    a = math.radians(roll)
    # Over to its side about its feet, and drawn back half its height so it
    # lies across the middle of the frame, lifted by its own thickness.
    v["base"] = (0, roll, 0)
    v["@base"] = (-1.25 * math.sin(a), 0.0, 0.42 * math.sin(a) - 0.30 * (1.0 - over))
    v["_z"] = 0.0
    v["~sword"] = -0.99
    v["~dropped"] = 0.0
    if frame == 5:
        v.update({"&pauldron_l": (0.10, -0.30, -0.12), "&pauldron_r": (-0.05, 0.30, 0.10),
                  "&head": (-0.30, -0.25, -0.05), "head": (50, 0, 70), "pauldron_l": (0, 40, 0),
                  "pauldron_r": (30, 0, 0), "~core": -0.75})
    return v


# =================================================================================
#  The Nightmare Anchor (the Guild Hall's)
# =================================================================================
def lathe(profile, segments=28):
    """A solid turned about Z from (radius, z) points listed top to bottom --
    a bell. The two ends close on the axis."""
    key = ("lathe", tuple(profile), segments)
    if key in bc._meshes:
        return bc._meshes[key]
    bm = bmesh.new()
    rings = []
    for (rad_, z) in profile:
        ring = []
        for i in range(segments):
            a = i / segments * math.tau
            ring.append(bm.verts.new((rad_ * math.cos(a), rad_ * math.sin(a), z)))
        rings.append(ring)
    for r0, r1 in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("lathe")
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    bc._meshes[key] = me
    return me


# A great bell, mouth down: crown, shoulder, waist, and the lip flaring out.
BELL = ((0.001, 0.64), (0.12, 0.63), (0.21, 0.60), (0.26, 0.54), (0.28, 0.44), (0.285, 0.32), (0.30, 0.20),
        (0.34, 0.10), (0.40, 0.03), (0.45, -0.01), (0.44, -0.04), (0.001, -0.02))

# The loops of the knot: (major radius, thread radius, tilt about X, tilt about
# Y, turn about Z, centre offset, which joint).
_KNOT = None


def _knot_loops():
    global _KNOT
    if _KNOT is None:
        import random
        rnd = random.Random(1904)
        loops = []
        # Enough of them, and thick enough, that it is a knot the bell shows
        # through and not a cage the bell sits in.
        for k in range(22):
            major = rnd.uniform(0.40, 0.60)
            loops.append((major, rnd.uniform(0.036, 0.058), rnd.uniform(-80, 80), rnd.uniform(-80, 80),
                          rnd.uniform(0, 180), (rnd.uniform(-0.08, 0.08), rnd.uniform(-0.06, 0.06),
                                                rnd.uniform(-0.12, 0.16)), ("knot_a", "knot_b", "knot_c")[k % 3]))
        _KNOT = loops
    return _KNOT


def build_nightmare_anchor():
    """The great Anchor behind the Guild Hall's desk: a knot of black thread a
    good deal taller than a man, wound round and round the last Dawn Bell of
    Havenbrook, which glows pale through the gaps; dream smoke boiling round
    its foot, roots of thread running out across the floor, and strands
    hanging loose off it that lash out across the hall when it fights. It
    does not move from where it is rooted."""
    r = Rig()
    r.joint("base", (0, 0, 0))
    # The bell, hung in the middle of the knot.
    r.joint("bell", (0, 0, 0.62), "base")
    r.add("bell_body", lathe(BELL), "anc_bell", "bell", loc=(0, 0, 0.0))
    r.add("bell_lip", TORUS(0.43, 0.035), "anc_bell_glow", "bell", loc=(0, 0, -0.01))
    r.add("bell_band", TORUS(0.290, 0.022), "anc_bell_dk", "bell", loc=(0, 0, 0.30))
    r.add("crown", TORUS(0.07, 0.03), "anc_bell_dk", "bell", loc=(0, 0, 0.67), rot=(math.radians(90), 0, 0))
    r.add("clapper", E(0.085, 0.085, 0.085), "anc_bell_dk", "bell", loc=(0, 0, -0.07))
    # The glow of it, kept just inside the bell so it shows only through the
    # thread's gaps and under the lip.
    r.add("bell_glow", E(0.36, 0.36, 0.08), "anc_bell_glow", "bell", loc=(0, 0, 0.0))

    # The knot: loops of thread every way round the bell, on three joints so
    # it can breathe and writhe a little out of step.
    for name in ("knot_a", "knot_b", "knot_c"):
        r.joint(name, (0, 0, 0.92), "base")
    for k, (major, minor, rx, ry, rz, off, joint) in enumerate(_knot_loops()):
        colour = ("anc_thread", "anc_thread_lt", "anc_thread", "anc_sheen")[k % 4]
        r.add("loop", TORUS(major, minor), colour, joint, loc=off,
              rot=(math.radians(rx), math.radians(ry), math.radians(rz)))
    # Thick cords round its waist and over its crown, so it is a knot and not
    # a ball of loose loops.
    r.add("cord_waist", TORUS(0.52, 0.065), "anc_thread_lt", "knot_a", loc=(0, 0, -0.10),
          rot=(math.radians(10), math.radians(-8), 0))
    r.add("cord_cross", TORUS(0.50, 0.060), "anc_thread", "knot_b", loc=(0, 0, 0.05),
          rot=(math.radians(84), 0, math.radians(30)))
    r.add("cord_cross2", TORUS(0.48, 0.055), "anc_thread_lt", "knot_c", loc=(0, 0, 0.02),
          rot=(math.radians(80), 0, math.radians(-40)))

    # Roots: cords out across the floor from the foot of it.
    for k in range(9):
        a = math.radians(k * 40 + 12)
        far = 0.80 + 0.18 * (k % 3)
        r.limb("root", (math.cos(a) * 0.28, math.sin(a) * 0.22, 0.42), (math.cos(a) * 0.58, math.sin(a) * 0.48, 0.10),
               0.075, ("anc_thread", "anc_thread_lt")[k % 2], "base", r_tip=0.055)
        r.limb("root_end", (math.cos(a) * 0.58, math.sin(a) * 0.48, 0.10), (math.cos(a) * far, math.sin(a) * far * 0.85,
                                                                          0.02), 0.055, "anc_thread", "base",
               r_tip=0.012)

    # Dream smoke boiling round the foot and clinging to the knot.
    # Low and flat and run together, the way smoke lies: round, the puffs
    # were a ring of grapes.
    r.joint("smoke", (0, 0, 0), "base")
    for k, (x, y, z, rx, ry, rz) in enumerate(((-0.50, -0.12, 0.12, 0.30, 0.20, 0.09), (0.46, -0.08, 0.13, 0.28, 0.20, 0.09),
                                               (-0.14, -0.42, 0.09, 0.34, 0.16, 0.07), (0.24, -0.40, 0.10, 0.26, 0.15, 0.08),
                                               (-0.40, 0.30, 0.14, 0.26, 0.20, 0.09), (0.40, 0.30, 0.15, 0.24, 0.20, 0.09),
                                               (-0.60, 0.04, 0.42, 0.14, 0.12, 0.10), (0.58, 0.02, 0.50, 0.13, 0.11, 0.09),
                                               (-0.36, -0.18, 1.34, 0.12, 0.10, 0.07), (0.30, 0.10, 1.46, 0.10, 0.09, 0.06))):
        r.add("puff", E(rx, ry, rz), ("anc_smoke", "anc_smoke_lt")[k % 2], "smoke", loc=(x, y, z))

    # The strands that lash: each a joint at the knot with two lengths on it,
    # the second on a joint of its own so the end can whip.
    for k, (a, z) in enumerate(((-150, 1.10), (-30, 1.10), (-100, 0.70), (-60, 0.76), (150, 1.00), (30, 1.00))):
        ang = math.radians(a)
        name = "lash%d" % k
        r.joint(name, (math.cos(ang) * 0.42, math.sin(ang) * 0.34, z), "base", rest=(0, 0, a))
        # Built hanging out along the joint's +X and drooping: rest is the
        # strand curled down the side of the knot.
        r.limb("strand", (0, 0, 0), (0.28, 0, -0.18), 0.040, ("anc_thread_lt", "anc_thread")[k % 2], name,
               r_tip=0.032)
        r.joint(name + "_end", (0.28, 0, -0.18), name, rest=(0, 30, 0))
        r.limb("strand_end", (0, 0, 0), (0.32, 0, -0.06), 0.032, "anc_sheen" if k % 3 == 0 else "anc_thread",
               name + "_end", r_tip=0.008)
    return fitted(r, "nightmare_anchor", 104, {})


def anc_idle(t):
    # It breathes: the knot swells and settles, its loops writhe a little out
    # of step, the smoke drifts, and the strands stir.
    s, c = sn(t), sn(t, 0.25)
    v = {"_s": 0.025 * s, "knot_a": (0, 0, 6 * s), "knot_b": (3 * c, 0, -5 * s), "knot_c": (0, 4 * c, 4 * c),
         "~knot_b": 0.02 * c, "smoke": (0, 0, 8 * s), "@smoke": (0, 0, 0.02 * c), "~bell": 0.0}
    for k in range(6):
        v["lash%d" % k] = (0, 6 * sn(t, k * 0.17), 0)
        v["lash%d_end" % k] = (0, 10 * sn(t, k * 0.17 + 0.2), 0)
    return v


def anc_walk(t):
    # Rooted: what walks it is the idle.
    return anc_idle(t)


def _lash(spread, curl, lift=0.0, side=0.0):
    """The strands flung out: `spread` raises them from hanging to straight out
    (about the joint's Y), `curl` whips the ends, `side` swings the whole set
    round one way."""
    v = {}
    for k in range(6):
        v["lash%d" % k] = (lift, -spread, side)
        v["lash%d_end" % k] = (0, -curl, 0)
    return v


ANC_LASH = [
    dict(_lash(0, 0)),
    dict(_lash(-20, -20), **{"_s": -0.06, "knot_a": (0, 0, -10), "knot_b": (0, 0, 10)}),
    dict(_lash(26, 30, side=-40), **{"_s": 0.02, "knot_a": (0, 0, 12)}),
    dict(_lash(36, 50, side=20), **{"_s": 0.06, "knot_b": (0, 0, -14)}),
    dict(_lash(42, 64, side=50), **{"_s": 0.07, "knot_c": (6, 0, 10)}),
    dict(_lash(16, 16, side=10), **{"_s": 0.01}),
]


def anc_attack(t):
    v = dict(ANC_LASH[min(5, int(round(t * 5)))])
    v.setdefault("~bell", 0.0)
    return v


def anc_hurt(t):
    k = math.sin(t * math.pi)
    v = {"_squash": (1.0 + 0.08 * k, 1.0 + 0.08 * k, 1.0 - 0.10 * k), "knot_a": (0, 0, -10 * k),
         "knot_b": (8 * k, 0, 6 * k), "~smoke": 0.25 * k, "~bell": 0.0}
    v.update(_lash(-15 * k, 30 * k))
    return v


def anc_death(t):
    """It comes undone: the loops slacken and spread into flat rings that grow
    and thin away, the strands drop, the smoke billows and is gone, and the
    bell's light goes out of it with them -- nothing left but a few cords
    lying on the floor."""
    k = ease(t)
    # The bell stands bare for a moment once the thread is off it, and is
    # gone with the rest on the last frame: the game's own bell, in its frame,
    # is what is left standing there.
    v = {"~bell": -0.99 if t > 0.95 else 0.0, "smoke": (0, 0, 30 * k),
         "~smoke": 0.4 * math.sin(min(1.0, t * 1.4) * math.pi) - 0.99 * ease((t - 0.6) / 0.4)}
    for i, name in enumerate(("knot_a", "knot_b", "knot_c")):
        grow = 1.0 + 0.9 * k + 0.15 * i * k
        flat = max(0.05, 1.0 - 0.95 * ease(t * 1.2))
        thin = max(0.0, 1.0 - ease((t - 0.35) / 0.65))
        v["%" + name] = (grow * max(0.01, thin) ** 0.3, grow * max(0.01, thin) ** 0.3, flat * max(0.01, thin))
        v[name] = (0, 0, (20 + 15 * i) * k)
        v["@" + name] = (0, 0, -0.75 * k)
    v.update(_lash(-60 * k, -40 * k))
    for kk in range(6):
        v["~lash%d" % kk] = -0.99 * ease((t - 0.7) / 0.3)
    return v


# =================================================================================
#  Elder Vask, awake (scenes 23 and 50)
# =================================================================================
# His rig is blender_creatures.build_vask: the man and the chair on one joint
# at the rockers ("rock"), the hands resting on the stick across his knees.
def vask_fist(t):
    """He looks up, grins, and holds out a closed fist to be bumped: leant
    forward out of the chair, the right arm out straight at the player, and
    held there -- the last frame is the one the game keeps."""
    frame = min(3, int(round(t * 3)))
    k = (0.0, 0.55, 1.0, 1.0)[frame]
    # Up at the height of his shoulder and in a little across him: lower, the
    # fist was lost against his knees in the row that faces the camera.
    v = {"rock": X(-4 * k), "chest": X(12 * k + (1.0 if frame == 3 else 0.0)), "neck": X(-6 * k),
         "head": X(-8 * k), "shoulder_r": (-88 * k, 0, -12 * k), "elbow_r": X(64 * k), "hand_r": X(-10 * k)}
    return v


def vask_fury(t):
    """The dragon's shadow has gone over: he has stopped rocking, the chair
    tipped forward under him, both hands gripping its arms, leant out over his
    knees glaring at the sky -- and held, shaking."""
    frame = min(3, int(round(t * 3)))
    k = (0.0, 0.6, 1.0, 1.0)[frame]
    shake = (0.0, 0.0, 1.0, -1.0)[frame]
    # Leant forward from the hips but the head thrown back to the sky; the
    # arms swung back along his sides so the hands come down on the chair's
    # arms either side of him, off the stick in his lap.
    return {"rock": X(-6 * k), "chest": X(14 * k + 1.5 * shake), "neck": X(-22 * k), "head": (-24 * k, 0, 3 * shake),
            "shoulder_l": (34 * k, 10 * k, 0), "shoulder_r": (34 * k, -10 * k, 0),
            "elbow_l": X(10 * k), "elbow_r": X(10 * k)}


# =================================================================================
#  Elder Vask on his feet, in his own dream (scene 19)
# =================================================================================
def build_vask_stand():
    """Scene 19: "A ring of Hushed closes in on Elder Vask, who stands in front
    of his chair swinging his cane at them." The same old man as the one in
    the chair (blender_creatures.build_vask) -- robe, shawl, white beard, the
    stoop -- stood up: feet planted a little apart, his robe down past his
    knees, and his stick in his right hand like a sword. The chair is not in
    the rig; a chair that turned whenever he did would be furniture following
    him about. It stands behind him on its own (build_vask_chair)."""
    r = cr.Rig()
    r.joint("pelvis", (0, 0, 0.50))
    r.add("hips", E(0.15, 0.13, 0.10), "vask_robe", "pelvis")
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.075, 0, -0.02), "pelvis", rest=(0, sx * -4, 0))
        r.limb("thigh", (0, 0, 0), (0, 0, -0.24), 0.07, "vask_robe", "hip_" + side, r_tip=0.06)
        r.joint("knee_" + side, (0, 0, -0.24), "hip_" + side, rest=(-6, 0, 0))
        r.limb("shin", (0, 0, 0), (0, 0, -0.23), 0.055, "vask_robe_dk", "knee_" + side, r_tip=0.045)
        r.add("boot", E(0.06, 0.09, 0.045), "boot", "knee_" + side, loc=(0, -0.04, -0.24))
    # An old man's long robe, to below the knee: most of what tells him from a
    # townsman at forty pixels, as the beard is the rest.
    r.add("skirt", C(0.15, 0.20, 0.30, squash_y=0.85), "vask_robe", "pelvis", loc=(0, 0.0, -0.02))
    r.add("skirt_hem", E(0.19, 0.165, 0.035), "vask_robe_dk", "pelvis", loc=(0, 0.0, -0.33))

    # Stooped as he is in the chair, a little more for standing.
    r.joint("chest", (0, 0.01, 0.08), "pelvis", rest=(18, 0, 0))
    r.add("torso", E(0.17, 0.13, 0.21), "vask_robe", "chest", loc=(0, 0, 0.18))
    r.add("shawl", E(0.21, 0.17, 0.09), "vask_shawl", "chest", loc=(0, 0.01, 0.30))
    r.add("shawl_front", E(0.10, 0.06, 0.14), "vask_shawl", "chest", loc=(0, -0.10, 0.24))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.16, 0, 0.32), "chest", rest=(0, sx * -10, 0))
        r.limb("upper", (0, 0, 0), (0, 0, -0.20), 0.05, "vask_robe", "shoulder_" + side, r_tip=0.045)
        r.joint("elbow_" + side, (0, 0, -0.20), "shoulder_" + side, rest=(-20, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.20), 0.045, "vask_robe_dk", "elbow_" + side, r_tip=0.04)
        r.joint("hand_" + side, (0, 0, -0.20), "elbow_" + side)
        r.add("hand", E(0.045, 0.05, 0.04), "vask_skin_dk", "hand_" + side)
    # The stick in his right fist, gripped near the knob and carried on along
    # the hand's -Z, the way a sword is -- chunkier than a real cane, or at
    # forty pixels it is all outline.
    r.add("stick", C(0.024, 0.02, 0.52), "vask_stick", "hand_r", loc=(0, -0.01, 0.05))
    r.add("stick_knob", E(0.04, 0.04, 0.04), "vask_stick", "hand_r", loc=(0, -0.01, 0.07))

    r.joint("neck", (0, -0.03, 0.38), "chest", rest=(-12, 0, 0))
    r.add("neckp", C(0.05, 0.055, 0.07), "vask_skin_dk", "neck", loc=(0, 0, 0.05))
    r.joint("head", (0, -0.01, 0.09), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.115, 0.12, 0.115), "vask_skin", "head", loc=(0, 0, 0.06))
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
    # The seated man's scale, a touch smaller stood up: stooped and old, he is
    # about the hero's height and no more.
    r.pose.scale = (2.3, 2.3, 2.3)
    return r


# His guard: the stick held up and out to his right, a little before him, the
# free hand out for balance, one foot forward. Every angle of the stick in
# this clip was aimed at what the camera sees, not at what a man would do: at
# forty-six degrees overhead anything pointed at the camera is drawn pointing
# down the screen, so a stick held out in front of him reads, from the front,
# as one hanging at his side. Held out to the side and up it reads from all
# four. (The shoulder angles were solved for the stick's direction offline.)
_VASK_GUARD = {"shoulder_r": (15, -132, -12), "elbow_r": X(0), "hand_r": X(0),
               "shoulder_l": fwd(28), "elbow_l": X(-46),
               "hip_l": fwd(12), "hip_r": fwd(-10), "knee_l": X(10), "knee_r": X(6)}


def vask_stand_idle(t):
    s = sn(t)
    v = dict(_VASK_GUARD)
    v.update({"chest": X(2.0 * s), "neck": X(-1.0 * s), "head": X(-2.0 * sn(t, 0.2)), "_z": -0.006 * abs(s),
              "shoulder_r": (15 + 3 * s, -132, -12)})
    return v


def vask_stand_walk(t):
    # He holds his ground; a shuffle in place is all a walk is for him.
    v = dict(_VASK_GUARD)
    v.update({"hip_l": fwd(12 + 8 * sn(t)), "hip_r": fwd(-10 - 8 * sn(t)), "_z": 0.01 * abs(sn(t))})
    return v


def _vask_cut(t):
    """One cut of the stick, a chop over the top: drawn back high over his
    right shoulder, over his head, and down across in front of him to his
    left at whatever is there, turning into it -- then back to his guard.
    Over the top, not straight down or straight across: from the front a
    downward cut comes at the camera and hardly moves, and a sweep across
    passes through pointing at it. Euler angles are mixed one by one, so the
    "over" key is what keeps the stick going over his head between the two
    and not round by his knees."""
    i, k = phases(t, 0.32, 0.47, 0.63, 1.0)
    guard = dict(_VASK_GUARD)
    raise_ = dict(_VASK_GUARD)
    raise_.update({"shoulder_r": (27, -138, -6), "elbow_r": X(10), "chest": (-14, 0, 15), "head": (0, 0, -8),
                   "_y": 0.03})
    over = dict(_VASK_GUARD)
    over.update({"shoulder_r": (12, -180, -6), "elbow_r": X(10), "chest": X(-4)})
    cut = dict(_VASK_GUARD)
    cut.update({"shoulder_r": (-81, -180, -21), "elbow_r": X(10), "chest": (14, 0, -20), "head": (0, 0, 10),
                "_y": -0.08, "hip_l": fwd(22), "hip_r": fwd(-16), "knee_l": X(16)})
    return [mix(guard, raise_, k), mix(raise_, over, k), mix(over, cut, k), mix(cut, guard, k), guard][i]


def vask_stand_attack(t):
    return _vask_cut(t)


def vask_fend(t):
    """Swinging his cane at them, over and over: a cut, half a breath in his
    guard, and the next -- scene 19 and the whole of his dream after it."""
    return _vask_cut(min(1.0, t / 0.8))


def vask_stand_hurt(t):
    k = math.sin(t * math.pi)
    v = dict(_VASK_GUARD)
    v.update({"chest": X(18 - 14 * k), "_y": 0.10 * k, "neck": X(-10 * k), "head": (0, 0, 10 * k)})
    return v


def vask_stand_death(t):
    # He cannot be killed in the dream (scene 19): the clip a monster would
    # die by is only ever a stagger.
    return vask_stand_hurt(min(1.0, t))


def build_vask_chair():
    """His rocking chair with nobody in it, rendered by itself so the dream can
    stand him in front of it: the very chair the man in it rocks in
    (blender_creatures.vask_chair), at his scale and tipped back as it rests.
    tools/make_vask_chair.py cuts the facing frame out as a prop."""
    r = cr.Rig()
    r.joint("rock", (0, 0.04, 0.05), rest=(-2, 0, 0))
    cr.vask_chair(r, "rock")
    r.pose.scale = (2.4, 2.4, 2.4)
    return r


def vask_chair_still(t):
    return {}


# =================================================================================
#  Registration
# =================================================================================

# id: (builder, frame px, (idle, walk, attack, hurt, death), shadow radius)
CREATURES = {
    "hushed": (build_hushed, 80, (hush_idle, hush_walk, hush_attack, hush_hurt, hush_death), 0.30),
    "ashen_vanguard": (build_ashen_vanguard, 144, (av_idle, av_walk, av_attack, av_hurt, av_death), 0.62),
    "forge_demon": (build_forge_demon, 256, (fd_idle, fd_walk, fd_attack, fd_hurt, fd_death), 1.45),
    "nightmare_anchor": (build_nightmare_anchor, 160, (anc_idle, anc_walk, anc_attack, anc_hurt, anc_death), 1.10),
    # Eighty: his stick, raised for a cut, goes up past his head.
    "vask_stand": (build_vask_stand, 80,
                   (vask_stand_idle, vask_stand_walk, vask_stand_attack, vask_stand_hurt, vask_stand_death), 0.34),
    "vask_chair": (build_vask_chair, 64,
                   (vask_chair_still, vask_chair_still, vask_chair_still, vask_chair_still, vask_chair_still), 0.40),
}

# id: {clip: (pose, frames, loops)} -- see EXTRA_CLIPS in blender_creatures.py.
EXTRA = {
    # The berserker's sprint, authored as itself, and the two-handed sunder.
    "ashen_vanguard": {"run": (av_run, 6, True), "heavy": (av_heavy, 6, False)},
    # The wide sweep, the breath of flame, and the whirlwind.
    "forge_demon": {"heavy": (fd_heavy, 6, False), "flame": (fd_flame, 6, False), "spin": (fd_spin, 8, True)},
    # Elder Vask awake: the fist held out to be bumped, and his fury at the
    # dragon's shadow. Both are held on their last frame.
    "vask": {"fist": (vask_fist, 4, False), "fury": (vask_fury, 4, False)},
    # On his feet in his own dream, fending off the Hushed with his stick.
    "vask_stand": {"fend": (vask_fend, 8, True)},
}


def register():
    cr.CREATURES.update(CREATURES)
    for name, clips in EXTRA.items():
        cr.EXTRA_CLIPS.setdefault(name, {}).update(clips)
