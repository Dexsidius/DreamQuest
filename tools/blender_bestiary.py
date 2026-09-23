# =============================================================================
#  blender_bestiary.py - the twenty-five monsters that fill the level ladder:
#  the Bayou's ten, Hollowrest Crypt's twelve, and the three past them -- and
#  the Cinder King, who rules the Brimstone Palace at the top of it.
#
#  Rendered by tools/make_creatures.ps1 like every other monster:
#      .\tools\make_creatures.ps1 -Only fen_gator,bog_lurker
#
#  They are built out of the same parts as the monsters in
#  blender_creatures.py -- its Rig, the capsule and ellipsoid meshes, the
#  humanoid limbs, cel shading, majority reduction and the selective outline --
#  and register themselves into its CREATURES table, so a sheet of a Fen Gator
#  is made exactly the way a rat's is. That file hands itself to this one
#  (see the foot of it) rather than being imported by name.
#
#  Two things differ from the first monsters, both so twenty-five of them could
#  be written without guessing:
#
#  * Each one is **fitted to a height on screen**. Rig.fit measures the rig
#    through the camera, facing it at rest, and scales it until it stands that
#    many game pixels tall -- instead of a scale factor tried until it looked
#    right. The heights were set against what these monsters were drawn as
#    when they were other monsters' sheets tinted, so none of them changes size
#    in the world by much.
#  * Offsets in a pose (_x, _y, _z and the world-space fall) are in the
#    **model's own units**, scaled with it. A lunge written for a ghoul is the
#    same lunge on the Bone Colossus, three times the size.
# =============================================================================

import math

import bpy  # noqa: F401  (the rigs are Blender objects; kept for readers)

import blender_character as bc
import blender_creatures as cr

E, C = cr.E, cr.C
X, fwd, sn, ease, phases, mix = cr.X, cr.fwd, cr.sn, cr.ease, cr.phases, cr.mix
gait, topple = cr.gait, cr.topple
humanoid_legs, humanoid_arms = cr.humanoid_legs, cr.humanoid_arms
TORUS = bc.mesh_torus


# --- colours ---------------------------------------------------------------------------
bc.PALETTE.update({
    # The Bayou: mud, weed and standing water, and the lights in it
    "lurk": (0.35, 0.39, 0.25), "lurk_dk": (0.23, 0.27, 0.17), "lurk_belly": (0.66, 0.63, 0.44),
    "lurk_moss": (0.29, 0.44, 0.21), "lurk_claw": (0.20, 0.19, 0.14), "lurk_mouth": (0.16, 0.12, 0.10),
    "lurk_eye_glow": (1.00, 0.84, 0.30),
    "weed": (0.38, 0.52, 0.22), "weed_dk": (0.24, 0.34, 0.16), "moss_lt": (0.46, 0.60, 0.26),
    "toad": (0.47, 0.44, 0.27), "toad_dk": (0.33, 0.31, 0.19), "toad_belly": (0.82, 0.76, 0.54),
    "toad_wart": (0.60, 0.50, 0.26), "toad_sac": (0.90, 0.78, 0.46), "toad_mouth": (0.20, 0.14, 0.10),
    "toad_eye_glow": (0.98, 0.70, 0.18), "pupil": (0.08, 0.06, 0.05),
    "hag_skin": (0.62, 0.66, 0.50), "hag_skin_dk": (0.44, 0.48, 0.36), "hag_rag": (0.37, 0.33, 0.26),
    "hag_rag_dk": (0.26, 0.23, 0.18), "hag_moss": (0.34, 0.45, 0.24), "hag_locks": (0.64, 0.68, 0.60),
    "hag_nail": (0.24, 0.22, 0.18), "hag_eye_glow": (0.66, 1.00, 0.36), "hag_fire_glow": (0.54, 1.00, 0.50),
    "driftwood": (0.46, 0.40, 0.32), "driftwood_dk": (0.32, 0.27, 0.21),
    "peat": (0.28, 0.22, 0.16), "peat_dk": (0.19, 0.15, 0.11), "root": (0.43, 0.33, 0.22),
    "moss": (0.33, 0.47, 0.20), "shroom_cap": (0.80, 0.52, 0.28), "shroom_stalk": (0.88, 0.84, 0.72),
    "shroom_glow": (0.72, 1.00, 0.56),
    "gator": (0.30, 0.37, 0.23), "gator_dk": (0.20, 0.25, 0.15), "gator_belly": (0.76, 0.72, 0.50),
    "gator_eye_glow": (1.00, 0.80, 0.22), "tooth": (0.94, 0.91, 0.80),
    "mantis": (0.52, 0.58, 0.30), "mantis_dk": (0.35, 0.40, 0.20), "mantis_lt": (0.68, 0.72, 0.40),
    "mantis_wing": (0.60, 0.60, 0.40), "mantis_spine": (0.86, 0.84, 0.62), "mantis_eye_glow": (0.98, 0.64, 0.24),
    "drown": (0.56, 0.64, 0.66), "drown_dk": (0.40, 0.48, 0.52), "drown_rag": (0.34, 0.37, 0.36),
    "drown_hair": (0.14, 0.18, 0.18), "drown_eye_glow": (0.44, 1.00, 0.86), "barnacle": (0.84, 0.82, 0.74),
    "iron_rust": (0.45, 0.30, 0.20), "iron_rust_lt": (0.60, 0.43, 0.28), "net": (0.55, 0.50, 0.36),
    "witch": (0.40, 0.80, 0.58), "witch_lt": (0.66, 0.96, 0.74), "witch_glow": (0.90, 1.00, 0.86),
    "witch_eye": (0.08, 0.20, 0.14),
    "shaman": (0.30, 0.50, 0.46), "shaman_dk": (0.18, 0.33, 0.31), "shaman_robe": (0.44, 0.30, 0.21),
    "shaman_robe_dk": (0.31, 0.21, 0.15), "paint_red": (0.72, 0.20, 0.16), "feather_b": (0.84, 0.64, 0.24),
    "feather_r": (0.74, 0.26, 0.18), "feather_w": (0.92, 0.90, 0.84), "shaman_eye_glow": (0.60, 1.00, 0.90),
    "shaman_glow": (0.52, 1.00, 0.92),
    "fen": (0.27, 0.42, 0.34), "fen_dk": (0.16, 0.27, 0.22), "fen_lt": (0.38, 0.55, 0.42),
    "fen_belly": (0.84, 0.80, 0.60), "fen_mark": (0.82, 0.48, 0.20), "fen_eye_glow": (1.00, 0.62, 0.20),
    "reed": (0.66, 0.60, 0.34), "pearl_glow": (0.70, 1.00, 0.84),
    # Hollowrest Crypt: grave linen, old steel, and whatever keeps them moving
    "ghoul": (0.58, 0.62, 0.54), "ghoul_dk": (0.40, 0.44, 0.38), "ghoul_lt": (0.74, 0.76, 0.66),
    "ghoul_claw": (0.24, 0.22, 0.20), "ghoul_eye_glow": (0.96, 0.90, 0.36), "ghoul_mouth": (0.34, 0.10, 0.10),
    "archer_hood": (0.38, 0.31, 0.24), "archer_hood_dk": (0.27, 0.22, 0.17), "leather_dk": (0.30, 0.21, 0.14),
    "fletch": (0.16, 0.15, 0.16), "archer_bow": (0.44, 0.31, 0.19), "bowstring": (0.86, 0.84, 0.76),
    "linen": (0.78, 0.72, 0.56), "linen_dk": (0.56, 0.50, 0.37),
    "knight_steel": (0.54, 0.56, 0.58), "knight_steel_dk": (0.37, 0.39, 0.43), "knight_tabard": (0.52, 0.15, 0.14),
    "knight_blade": (0.74, 0.76, 0.78),
    "plague": (0.62, 0.66, 0.42), "plague_dk": (0.45, 0.40, 0.47), "plague_boil_glow": (0.88, 0.98, 0.42),
    "shroud_stain": (0.62, 0.58, 0.46), "shroud_stain_dk": (0.46, 0.42, 0.32), "plague_ooze": (0.56, 0.74, 0.26),
    "plague_gas": (0.60, 0.80, 0.34),
    "shade": (0.17, 0.14, 0.23), "shade_lt": (0.29, 0.25, 0.39), "shade_mask": (0.90, 0.88, 0.84),
    "shade_glow": (0.82, 0.58, 1.00),
    "ghound_fur": (0.25, 0.23, 0.27), "ghound_eye_glow": (0.52, 1.00, 0.72),
    "thrall_black": (0.18, 0.16, 0.19), "thrall_shirt": (0.84, 0.81, 0.76), "thrall_coat": (0.54, 0.13, 0.15),
    "thrall_skin": (0.82, 0.78, 0.76), "thrall_skin_dk": (0.64, 0.60, 0.60), "thrall_nail": (0.34, 0.28, 0.30),
    "thrall_hair": (0.16, 0.13, 0.13), "thrall_eye_glow": (1.00, 0.26, 0.24), "blood": (0.58, 0.07, 0.09),
    "bone_cavity": (0.16, 0.14, 0.13), "colossus_glow": (0.48, 0.96, 0.88),
    "nosf_coat": (0.15, 0.13, 0.17), "nosf_coat_dk": (0.09, 0.08, 0.11), "nosf_skin": (0.80, 0.84, 0.74),
    "nosf_skin_dk": (0.60, 0.64, 0.56),
    "warden_iron": (0.42, 0.46, 0.48), "warden_iron_dk": (0.28, 0.31, 0.34), "verdigris": (0.36, 0.58, 0.52),
    "warden_cloak": (0.21, 0.23, 0.28), "warden_glow": (0.58, 0.92, 1.00), "warden_blade": (0.66, 0.70, 0.72),
    "vamp_boot": (0.12, 0.10, 0.12), "vamp_boot_lt": (0.24, 0.20, 0.22), "vamp_doublet": (0.40, 0.08, 0.13),
    "vamp_waist": (0.20, 0.06, 0.09), "vamp_cravat": (0.92, 0.90, 0.86), "vamp_eye_glow": (1.00, 0.20, 0.20),
    "vamp_cape": (0.11, 0.09, 0.13), "vamp_lining": (0.66, 0.08, 0.13), "vamp_skin": (0.88, 0.86, 0.88),
    "vamp_hair": (0.10, 0.08, 0.11),
    # Past the crypt
    "rev_armour": (0.36, 0.35, 0.38), "rev_armour_dk": (0.23, 0.22, 0.25), "rev_glow": (1.00, 0.68, 0.28),
    "rev_bronze": (0.70, 0.52, 0.28),
    "rev_cloak": (0.38, 0.14, 0.10), "rev_skin": (0.52, 0.52, 0.47), "rev_blade": (0.62, 0.60, 0.56),
    "abyss": (0.22, 0.15, 0.29), "abyss_dk": (0.13, 0.09, 0.18), "abyss_glow": (1.00, 0.36, 0.86),
    "abyss_horn": (0.33, 0.29, 0.35),
    "rime_armour": (0.60, 0.68, 0.76), "rime_armour_dk": (0.40, 0.48, 0.58), "rime_frost": (0.88, 0.94, 0.98),
    "rime_glow": (0.62, 0.96, 1.00),
    # The Cinder King
    "king_skin": (0.36, 0.10, 0.10), "king_skin_dk": (0.22, 0.06, 0.07), "king_armour": (0.16, 0.14, 0.17),
    "king_armour_lt": (0.32, 0.28, 0.32), "king_gold": (0.86, 0.66, 0.26), "king_mantle": (0.10, 0.07, 0.09),
    "king_lining": (0.62, 0.08, 0.11), "king_glow": (1.00, 0.56, 0.16), "king_horn": (0.26, 0.21, 0.21),
    "king_blade": (0.13, 0.11, 0.13),
})


# --- the rig, fitted ---------------------------------------------------------------------
_fitted = {}
_OFFSETS = ("_x", "_y", "_z", "_wx", "_wy", "_wz")


class Rig(cr.Rig):
    """blender_creatures' Rig, fitted to a height and posed in its own units.

    Two keys it understands that the first rigs did not need: "_s" swells the
    whole body (a wisp flaring up, guttering out) and "~joint" swells one joint
    and everything on it (a toad's throat filling). Both are additions to 1, so
    a pose that does not mention them -- and anything mix() blends them with --
    leaves the size alone."""

    def fit(self, key, px, stance=None):
        """Measured in `stance` -- the creature's idle, as a pose dict -- when
        what it holds only stands up straight once it is posed."""
        if key not in _fitted:
            self.apply(stance or {})
            bpy.context.view_layer.update()
            _, up = bc.camera_basis()
            lo, hi = float("inf"), float("-inf")
            for ob in self.parts:
                m = ob.matrix_world
                for v in ob.data.vertices:
                    h = (m @ v.co).dot(up)
                    lo, hi = min(lo, h), max(hi, h)
            _fitted[key] = px * cr.UNITS_PER_PX / max(1e-6, hi - lo)
        s = _fitted[key]
        self.pose.scale = (s, s, s)
        return self

    def apply(self, values, turn=0.0):
        s = self.pose.scale[0]
        v = dict(values)
        for key in _OFFSETS:
            if key in v:
                v[key] *= s
        super().apply(v, turn)
        grow = 1.0 + values.get("_s", 0.0)
        if grow != 1.0:
            self.pose.scale = (s * grow, s * grow, s * grow)
        for key, value in values.items():
            if key.startswith("~") and key[1:] in self.j:
                g = max(0.01, 1.0 + value)
                self.j[key[1:]].scale = (g, g, g)


def fingers(r, side, colour, n=3, r_=0.012, reach=0.08, spread=0.03, down=0.06):
    for k in range(n):
        o = k - (n - 1) / 2.0
        r.limb("finger", (o * spread * 0.7, -0.02, -0.03), (o * spread, -reach, -0.03 - down), r_, colour,
               "hand_" + side, r_tip=r_ * 0.3)


def struck(k, **more):
    """The common flinch: the chest thrown back and the whole body knocked a
    step away, k running 0 -> 1 -> 0."""
    v = {"chest": X(-16 * k), "_y": 0.05 * k, "neck": X(-16 * k), "head": (0, 0, 10 * k)}
    v.update({key: tuple(a * k for a in val) if isinstance(val, tuple) else val * k for key, val in more.items()})
    return v


def swing(t, rest, wind, hit, marks=(0.38, 0.58, 1.0)):
    """A one-shot attack as three held poses: rest -> wind-up -> blow -> rest."""
    i, k = phases(t, *marks)
    return [mix(rest, wind, k), mix(wind, hit, k), mix(hit, rest, k), rest][i]


# =================================================================================
#  The Bayou
# =================================================================================

# --- Bog Lurker ------------------------------------------------------------------------
def build_bog_lurker():
    """What waits at the bottom of the lake for whoever walks its edge: all back
    and arm, its head sunk between its shoulders so the eyes ride highest --
    which is all of it anybody sees until it stands up. Moss on the hump, weed
    hanging off it, and a mouth the width of its face."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.50))
    r.add("hips", E(0.23, 0.19, 0.15), "lurk", "pelvis")
    r.add("weedskirt", E(0.20, 0.16, 0.10), "weed_dk", "pelvis", loc=(0, -0.04, -0.08))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.15, 0, -0.04), "pelvis", rest=(-18, 0, 0))
        r.limb("thigh", (0, 0, 0), (0, 0, -0.24), 0.12, "lurk", "hip_" + side, r_tip=0.09)
        r.joint("knee_" + side, (0, 0, -0.24), "hip_" + side, rest=(30, 0, 0))
        r.limb("shin", (0, 0, 0), (0, 0, -0.23), 0.085, "lurk_dk", "knee_" + side, r_tip=0.065)
        r.add("foot", E(0.10, 0.15, 0.045), "lurk_dk", "knee_" + side, loc=(0, -0.08, -0.22))
        for k in (-1, 0, 1):
            r.limb("toe", (k * 0.05, -0.13, -0.23), (k * 0.08, -0.23, -0.25), 0.026, "lurk_dk",
                   "knee_" + side, r_tip=0.01)

    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(36, 0, 0))
    r.add("torso", E(0.31, 0.25, 0.30), "lurk", "chest", loc=(0, 0, 0.24))
    r.add("belly", E(0.21, 0.13, 0.22), "lurk_belly", "chest", loc=(0, -0.15, 0.16))
    r.add("hump", E(0.32, 0.22, 0.21), "lurk_moss", "chest", loc=(0, 0.09, 0.38))
    for x, y, z, rr in ((-0.18, 0.13, 0.44, 0.08), (0.14, 0.15, 0.48, 0.07), (0.0, 0.20, 0.32, 0.09),
                        (0.22, 0.06, 0.34, 0.06), (-0.06, 0.02, 0.56, 0.07)):
        r.add("moss", E(rr, rr * 0.9, rr * 0.7), "moss_lt", "chest", loc=(x, y, z))
    # Weed hanging off it, as if it had only just come up.
    for k, x in enumerate((-0.26, -0.10, 0.08, 0.24)):
        r.limb("weed", (x, 0.04, 0.40), (x * 1.15, -0.02, 0.02 + 0.06 * (k % 2)), 0.032, "weed", "chest",
               r_tip=0.012)

    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.31, -0.02, 0.36), "chest", rest=(-30, sx * -16, 0))
        r.add("deltoid", E(0.13, 0.13, 0.12), "lurk_moss", "shoulder_" + side)
        r.limb("upper", (0, 0, 0), (0, 0, -0.30), 0.10, "lurk", "shoulder_" + side, r_tip=0.085)
        r.joint("elbow_" + side, (0, 0, -0.30), "shoulder_" + side, rest=(-14, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.30), 0.09, "lurk_dk", "elbow_" + side, r_tip=0.075)
        r.joint("hand_" + side, (0, 0, -0.30), "elbow_" + side)
        r.add("hand", E(0.10, 0.10, 0.075), "lurk_dk", "hand_" + side)
        for k in (-1, 0, 1):
            r.limb("claw", (k * 0.05, -0.05, -0.04), (k * 0.075, -0.11, -0.14), 0.024, "lurk_claw",
                   "hand_" + side, r_tip=0.005)

    r.joint("neck", (0, -0.14, 0.40), "chest", rest=(-30, 0, 0))
    r.joint("head", (0, -0.06, 0.02), "neck", rest=(-6, 0, 0))
    r.add("skull", E(0.19, 0.17, 0.11), "lurk", "head", loc=(0, -0.06, 0.04))
    r.add("brow", E(0.18, 0.10, 0.05), "lurk_moss", "head", loc=(0, -0.02, 0.12))
    r.add("mouth", E(0.17, 0.06, 0.025), "lurk_mouth", "head", loc=(0, -0.19, -0.01))
    r.joint("jaw", (0, 0.02, -0.02), "head")
    r.add("jawp", E(0.17, 0.16, 0.055), "lurk_belly", "jaw", loc=(0, -0.10, -0.03))
    for sx in (-1, 1):
        r.add("lid", E(0.065, 0.065, 0.06), "lurk_dk", "head", loc=(sx * 0.10, -0.10, 0.14))
        r.add("eye", E(0.045, 0.035, 0.042), "lurk_eye_glow", "head", loc=(sx * 0.10, -0.145, 0.155))
        # Barbels from the corners of the mouth, like a catfish that got up.
        r.limb("barbel", (sx * 0.15, -0.17, -0.01), (sx * 0.22, -0.22, -0.20), 0.022, "lurk_dk", "head",
               r_tip=0.006)
        for k in range(2):
            r.limb("tooth", (sx * (0.05 + k * 0.05), -0.20, -0.01), (sx * (0.05 + k * 0.05), -0.21, -0.07),
                   0.016, "tooth", "head", r_tip=0.004)
    return r.fit("bog_lurker", 40)


def lurker_idle(t):
    s = sn(t)
    return {"_z": 0.008 * s, "chest": X(3 * s), "head": X(-2 * s), "jaw": X(4 + 5 * max(0.0, s)),
            "shoulder_l": fwd(6 + 4 * s), "shoulder_r": fwd(6 - 4 * s)}


def lurker_walk(t):
    # A lumber: short legs, the long arms swung through to the ground, and the
    # whole weight rolling from side to side over them.
    v = gait(t, 24, 30, 30, 0.03, 4)
    v.update({"pelvis": (0, 7 * sn(t), 0), "chest": (4, 0, 7 * sn(t)), "head": (0, 0, -7 * sn(t)),
              "jaw": X(6)})
    return v


def lurker_attack(t):
    # Both arms up over the hump, and down on whoever it is.
    rear = {"chest": X(-26), "shoulder_l": fwd(-150), "shoulder_r": fwd(-150), "elbow_l": X(-30),
            "elbow_r": X(-30), "neck": X(10), "jaw": X(30), "_y": 0.04, "_z": 0.03}
    slam = {"chest": X(22), "shoulder_l": fwd(40), "shoulder_r": fwd(40), "elbow_l": X(-6), "elbow_r": X(-6),
            "jaw": X(12), "_y": -0.10, "_z": -0.03, "hip_l": fwd(20), "knee_r": X(20)}
    return swing(t, {}, rear, slam, (0.40, 0.58, 1.0))


def lurker_hurt(t):
    return struck(math.sin(t * math.pi), jaw=X(22), shoulder_l=fwd(-20), shoulder_r=fwd(-24))


def lurker_death(t):
    v = topple(t)
    v["jaw"] = X(28 * ease(t))
    return v


def lurker_swim(t):
    # Up to its shoulders: the hump, the head and the eyes on top of it, and
    # the arms working under the surface where nobody sees them.
    s = sn(t)
    return {"_z": -0.60 + 0.012 * s, "chest": X(-14 + 2 * s), "hip_l": fwd(40), "hip_r": fwd(40),
            "knee_l": X(60), "knee_r": X(60), "shoulder_l": (-30 - 18 * s, 0, -40), "shoulder_r": (-30 + 18 * s, 0, 40),
            "elbow_l": X(-50), "elbow_r": X(-50), "neck": X(-14), "jaw": X(3), "head": (0, 0, 5 * sn(t, 0.25))}


# --- Mire Croaker ------------------------------------------------------------------------
def build_mire_croaker():
    """A toad the size of a pig, warts and all, with a throat that fills before
    it spits. It squats with its front up, so the face is what faces you."""
    r = Rig()
    r.joint("body", (0, 0, 0.13), rest=(-14, 0, 0))
    r.add("body", E(0.21, 0.22, 0.12), "toad", "body")
    r.add("back", E(0.18, 0.18, 0.07), "toad_dk", "body", loc=(0, 0.04, 0.07))
    r.add("belly", E(0.18, 0.19, 0.07), "toad_belly", "body", loc=(0, -0.01, -0.06))
    for x, y in ((-0.10, -0.04), (0.09, 0.02), (-0.03, 0.11), (0.12, 0.12), (-0.14, 0.09), (0.03, -0.07),
                 (0.16, -0.04)):
        r.add("wart", E(0.03, 0.03, 0.022), "toad_wart", "body", loc=(x, y, 0.115 - abs(x) * 0.25))
    r.joint("head", (0, -0.16, 0.04), "body", rest=(12, 0, 0))
    r.add("skull", E(0.18, 0.12, 0.08), "toad", "head", loc=(0, -0.04, 0.0))
    r.add("mouth", E(0.17, 0.10, 0.013), "toad_mouth", "head", loc=(0, -0.07, -0.036))
    r.joint("jaw", (0, 0.02, -0.03), "head")
    r.add("jawp", E(0.17, 0.12, 0.035), "toad_belly", "jaw", loc=(0, -0.07, -0.02))
    r.joint("sac", (0, -0.07, -0.08), "head")
    r.add("sacp", E(0.10, 0.075, 0.065), "toad_sac", "sac", loc=(0, 0, -0.01))
    for sx, side in ((-1, "l"), (1, "r")):
        r.add("brow", E(0.06, 0.055, 0.05), "toad", "head", loc=(sx * 0.10, -0.02, 0.06))
        r.add("eye", E(0.045, 0.042, 0.045), "toad_eye_glow", "head", loc=(sx * 0.10, -0.05, 0.085))
        r.add("pupil", E(0.03, 0.01, 0.013), "pupil", "head", loc=(sx * 0.10, -0.092, 0.087))
        r.add("gland", E(0.05, 0.08, 0.03), "toad_wart", "head", loc=(sx * 0.13, 0.06, 0.05))
        j = "hind_" + side
        r.joint(j, (sx * 0.17, 0.08, -0.02), "body")
        r.add("thigh", E(0.085, 0.13, 0.075), "toad_dk", j)
        r.limb("shin", (0, 0.05, -0.03), (sx * 0.05, -0.12, -0.09), 0.042, "toad_dk", j, r_tip=0.03)
        r.add("webbed", E(0.06, 0.075, 0.016), "toad_dk", j, loc=(sx * 0.07, -0.17, -0.10))
        j = "fore_" + side
        r.joint(j, (sx * 0.12, -0.13, -0.05), "body")
        r.limb("arm", (0, 0, 0), (sx * 0.045, -0.05, -0.10), 0.038, "toad", j, r_tip=0.028)
        r.add("hand", E(0.045, 0.055, 0.016), "toad_dk", j, loc=(sx * 0.06, -0.08, -0.11))
    return r.fit("mire_croaker", 24)


def croak_idle(t):
    s = sn(t)
    # A toad at rest is a throat going in and out.
    return {"_z": 0.003 * s, "body": X(1.5 * s), "head": X(-1.5 * s), "~sac": 0.35 * max(0.0, sn(t * 2))}


def croak_walk(t):
    # A heavy hop: up, forward, down, and a beat sat still.
    k = t % 1.0
    lift = max(0.0, math.sin(k / 0.6 * math.pi)) if k < 0.6 else 0.0
    return {"_z": 0.08 * lift, "_y": -0.03 * lift, "body": X(-14 * lift), "head": X(8 * lift),
            "hind_l": X(-34 * lift), "hind_r": X(-34 * lift), "fore_l": X(24 * lift), "fore_r": X(24 * lift)}


def croak_attack(t):
    # Up on its forelegs, the throat fills, and it spits.
    draw = {"body": X(-20), "head": X(-12), "_z": 0.03, "~sac": 0.9, "fore_l": X(-10), "fore_r": X(-10)}
    spit = {"body": X(6), "head": X(10), "_y": -0.04, "jaw": X(38), "~sac": -0.1}
    return swing(t, {}, draw, spit, (0.45, 0.62, 1.0))


def croak_hurt(t):
    k = math.sin(t * math.pi)
    return {"_z": 0.02 * k, "_y": 0.04 * k, "body": X(-16 * k), "head": X(10 * k), "jaw": X(14 * k)}


def croak_death(t):
    # Over onto its back, legs in the air.
    k = ease(t * 1.15)
    return {"_roll": 170 * k, "_z": 0.10 * k * (1 - k) + 0.10 * k, "jaw": X(20 * k),
            "hind_l": X(-40 * k), "hind_r": X(-40 * k), "fore_l": X(30 * k), "fore_r": X(30 * k)}


# --- Swamp Hag --------------------------------------------------------------------------
def build_swamp_hag():
    """Bent double over a crooked staff with a skull and a green light hung
    off its crook, hair to her waist, a shawl of moss and a nose you could
    hang a pot on. What she throws is on the end of that staff."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.54))
    r.add("hips", E(0.14, 0.11, 0.10), "hag_rag", "pelvis")
    humanoid_legs(r, -0.02, 0.07, 0.25, 0.26, 0.036, "hag_skin", "hag_skin_dk", foot_col="hag_skin_dk")
    # Skirts of sackcloth to the shin, ragged at the hem.
    r.add("skirt", C(0.15, 0.23, 0.20, squash_y=0.85), "hag_rag", "pelvis", loc=(0, 0.01, 0.0))
    for k in range(5):
        r.add("tatter", E(0.055, 0.045, 0.07), "hag_rag_dk" if k % 2 else "hag_moss", "pelvis",
              loc=(-0.17 + k * 0.085, -0.07 + 0.02 * (k % 2), -0.40 - 0.03 * (k % 2)))
    r.joint("chest", (0, 0, 0.06), "pelvis", rest=(30, 0, 0))
    r.add("torso", E(0.14, 0.12, 0.19), "hag_rag", "chest", loc=(0, 0, 0.16))
    r.add("hump", E(0.13, 0.12, 0.12), "hag_rag_dk", "chest", loc=(0, 0.08, 0.28))
    r.add("shawl", E(0.20, 0.17, 0.09), "hag_moss", "chest", loc=(0, 0.02, 0.30))
    for k in range(3):
        r.limb("fringe", ((k - 1) * 0.10, -0.09, 0.28), ((k - 1) * 0.12, -0.14, 0.13), 0.036, "hag_moss",
               "chest", r_tip=0.008)
    r.add("charm", E(0.03, 0.02, 0.04), "bone_w", "chest", loc=(0.05, -0.14, 0.17))
    r.add("pouch", E(0.05, 0.04, 0.06), "hag_rag_dk", "pelvis", loc=(-0.14, -0.07, 0.02))
    humanoid_arms(r, 0.30, 0.15, 0.20, 0.19, 0.034, "hag_skin", "hag_skin_dk", flare=10)
    for side in ("l", "r"):
        r.add("sleeve", E(0.06, 0.06, 0.08), "hag_rag", "shoulder_" + side, loc=(0, 0, -0.06))
        fingers(r, side, "hag_nail", r_=0.011, reach=0.09, spread=0.035)
    r.joint("neck", (0, -0.04, 0.34), "chest", rest=(-24, 0, 0))
    r.joint("head", (0, -0.02, 0.05), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.085, 0.09, 0.095), "hag_skin", "head", loc=(0, 0, 0.06))
    r.add("chin", E(0.035, 0.04, 0.04), "hag_skin", "head", loc=(0, -0.07, -0.025))
    r.limb("nose", (0, -0.08, 0.075), (0, -0.18, 0.02), 0.024, "hag_skin_dk", "head", r_tip=0.01)
    r.add("wart", E(0.014, 0.014, 0.014), "hag_moss", "head", loc=(0.016, -0.14, 0.05))
    for sx in (-1, 1):
        r.add("eye", E(0.02, 0.012, 0.017), "hag_eye_glow", "head", loc=(sx * 0.036, -0.085, 0.085))
    r.add("scalp", E(0.09, 0.095, 0.06), "hag_locks", "head", loc=(0, 0.01, 0.12))
    for k, (x, y) in enumerate(((-0.08, -0.03), (0.08, -0.03), (-0.07, 0.05), (0.07, 0.05), (0.0, 0.08))):
        r.limb("lock", (x, y, 0.11), (x * 1.5, y + 0.05, -0.22 - 0.05 * (k % 2)), 0.03, "hag_locks", "head",
               r_tip=0.008)
    # The staff: a crooked length of driftwood, a skull on the crook and the
    # light she throws hung under it.
    r.limb("staff", (0, 0, 0.42), (0, 0, -0.58), 0.027, "driftwood", "hand_r", r_tip=0.022)
    r.limb("crook", (0, 0, 0.42), (0, -0.11, 0.54), 0.025, "driftwood", "hand_r", r_tip=0.018)
    r.limb("crook2", (0, -0.11, 0.54), (0, -0.17, 0.45), 0.02, "driftwood_dk", "hand_r", r_tip=0.012)
    r.add("skullcharm", E(0.045, 0.045, 0.05), "bone_w", "hand_r", loc=(0, -0.17, 0.40))
    r.add("witchfire", E(0.04, 0.04, 0.045), "hag_fire_glow", "hand_r", loc=(0, -0.17, 0.32))
    for k in range(2):
        r.limb("ribbon", (0, -0.05 - k * 0.04, 0.50), (0.03, -0.07 - k * 0.04, 0.36), 0.012,
               "paint_red" if k else "hag_moss", "hand_r", r_tip=0.006)
    return r.fit("swamp_hag", 40, HAG_REST)


# The staff stands up straight when the chest's lean, the arm's reach and the
# hand's turn cancel out: 30 (chest) - 40 (shoulder) - 15 - 40 (elbow) + 65 = 0.
HAG_REST = {"shoulder_r": fwd(40), "elbow_r": X(-40), "hand_r": X(65), "shoulder_l": fwd(20), "elbow_l": X(-50)}


def hag_idle(t):
    s = sn(t)
    v = dict(HAG_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 8 * sn(t, 0.3)), "shoulder_l": fwd(20 + 6 * s)})
    return v


def hag_walk(t):
    # A hobble: short steps, the staff planted with each one.
    v = gait(t, 20, 24, 0, 0.025, 4)
    v.update(HAG_REST)
    v.update({"shoulder_r": fwd(40 + 10 * sn(t)), "shoulder_l": fwd(20 - 10 * sn(t)),
              "head": (0, 0, 6 * sn(t, 0.2)), "pelvis": (0, 4 * sn(t), 0)})
    return v


def hag_attack(t):
    # The staff goes up, the light on it swells, and she throws it at you.
    raise_ = {"shoulder_r": fwd(-150), "elbow_r": X(-20), "hand_r": X(20), "chest": X(-16), "neck": X(-10),
              "shoulder_l": fwd(-40), "elbow_l": X(-70), "_z": 0.02}
    cast = {"shoulder_r": fwd(70), "elbow_r": X(-6), "hand_r": X(-10), "chest": X(22), "neck": X(8),
            "shoulder_l": fwd(60), "elbow_l": X(-10), "_y": -0.06}
    return swing(t, HAG_REST, raise_, cast, (0.42, 0.60, 1.0))


def hag_hurt(t):
    v = dict(HAG_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=fwd(-30)))
    return v


def hag_death(t):
    return topple(t, armed=True)


# --- Rot Shambler -----------------------------------------------------------------------
def build_rot_shambler():
    """Peat that got up. A heap of bog earth on two stumps of root, with a club
    of root and mud for one arm, mushrooms on its shoulders and, pressed into
    the front of it, the skull of whoever the bog took first."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.46))
    r.add("hips", E(0.25, 0.20, 0.16), "peat", "pelvis")
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.15, 0, -0.04), "pelvis")
        r.limb("thigh", (0, 0, 0), (0, 0, -0.20), 0.115, "peat", "hip_" + side, r_tip=0.10)
        r.joint("knee_" + side, (0, 0, -0.20), "hip_" + side)
        r.limb("shin", (0, 0, 0), (0, 0, -0.18), 0.10, "peat_dk", "knee_" + side, r_tip=0.12)
        for k in (-1, 0, 1):
            r.limb("rootlet", (k * 0.05, -0.02, -0.20), (k * 0.13, -0.11, -0.25), 0.03, "root",
                   "knee_" + side, r_tip=0.008)
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(18, 0, 6))
    r.add("mass", E(0.35, 0.28, 0.32), "peat", "chest", loc=(0, 0, 0.26))
    r.add("lump", E(0.21, 0.19, 0.17), "peat_dk", "chest", loc=(-0.17, 0.06, 0.46))
    r.add("lump2", E(0.19, 0.17, 0.18), "peat", "chest", loc=(0.18, 0.05, 0.45))
    r.add("moss", E(0.27, 0.21, 0.10), "moss", "chest", loc=(0.02, 0.07, 0.56))
    r.add("moss2", E(0.12, 0.10, 0.08), "moss_lt", "chest", loc=(-0.22, 0.02, 0.52))
    # Roots and weed hanging off it everywhere.
    for x, y, z, l in ((-0.28, -0.06, 0.30, 0.26), (-0.12, -0.24, 0.20, 0.22), (0.10, -0.25, 0.26, 0.20),
                       (0.30, -0.08, 0.24, 0.24), (0.02, -0.26, 0.05, 0.16)):
        r.limb("root", (x, y, z), (x * 1.1, y - 0.03, z - l), 0.025, "root" if x < 0 else "weed", "chest",
               r_tip=0.008)
    for x, y, z, s in ((0.26, 0.02, 0.56, 1.0), (0.33, 0.10, 0.46, 0.7), (-0.20, 0.10, 0.62, 0.85),
                       (-0.30, 0.04, 0.50, 0.6)):
        r.limb("stalk", (x, y, z), (x, y, z + 0.08 * s), 0.022 * s, "shroom_stalk", "chest", r_tip=0.02 * s)
        r.add("cap", E(0.065 * s, 0.065 * s, 0.032 * s), "shroom_cap", "chest", loc=(x, y, z + 0.09 * s))
    r.add("glowcap", E(0.03, 0.03, 0.02), "shroom_glow", "chest", loc=(0.12, -0.18, 0.44))
    # A face -- somebody's -- pressed into the front of it.
    r.joint("head", (0.03, -0.25, 0.42), "chest", rest=(-26, 0, 0))
    r.add("hood", E(0.17, 0.10, 0.15), "peat_dk", "head", loc=(0, 0.04, 0.03))
    r.add("skull", E(0.125, 0.10, 0.13), "bone_w", "head")
    r.add("cheek", E(0.10, 0.07, 0.06), "bone_g", "head", loc=(0, -0.03, -0.06))
    r.add("jaw", E(0.08, 0.065, 0.045), "bone_g", "head", loc=(0, -0.02, -0.12))
    for sx in (-1, 1):
        r.add("socket", E(0.04, 0.025, 0.038), "rot_cloth_dk", "head", loc=(sx * 0.05, -0.085, 0.025))
        r.add("eye", E(0.024, 0.014, 0.024), "rot_eye_glow", "head", loc=(sx * 0.05, -0.10, 0.025))
    r.limb("vine", (-0.10, -0.06, 0.10), (-0.13, -0.10, -0.14), 0.02, "weed", "head", r_tip=0.008)
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.33, 0, 0.40), "chest", rest=(-16, sx * -14, 0))
        r.limb("upper", (0, 0, 0), (0, 0, -0.26), 0.10, "peat", "shoulder_" + side, r_tip=0.085)
        r.joint("elbow_" + side, (0, 0, -0.26), "shoulder_" + side, rest=(-10, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.26), 0.085, "peat_dk", "elbow_" + side,
               r_tip=0.10 if side == "r" else 0.06)
        r.joint("hand_" + side, (0, 0, -0.26), "elbow_" + side)
    # The right arm ends in a club of root and mud; the left in root fingers.
    r.add("club", E(0.16, 0.15, 0.18), "root", "hand_r", loc=(0, 0, -0.06))
    r.add("clubmud", E(0.12, 0.12, 0.10), "peat_dk", "hand_r", loc=(0.03, 0.03, 0.02))
    for k in range(4):
        a = k * math.pi / 2 + 0.4
        r.limb("thorn", (math.cos(a) * 0.10, math.sin(a) * 0.10, -0.08),
               (math.cos(a) * 0.22, math.sin(a) * 0.22, -0.12), 0.03, "root", "hand_r", r_tip=0.006)
    fingers(r, "l", "root", n=4, r_=0.022, reach=0.12, spread=0.05, down=0.10)
    return r.fit("rot_shambler", 36)


def shamble_idle(t):
    s = sn(t)
    return {"_z": 0.006 * s, "chest": (18 + 2 * s, 0, 6), "head": (0, 0, 5 * sn(t, 0.3)),
            "shoulder_l": fwd(10 + 4 * s), "shoulder_r": fwd(6 - 3 * s), "elbow_l": X(-20)}


def shamble_walk(t):
    # One leg drags and the club swings like a weight on a rope.
    s = sn(t)
    return {"hip_l": fwd(24 * s), "hip_r": fwd(-12 * s), "knee_l": X(18 * max(0.0, -s)), "knee_r": X(6),
            "_z": 0.02 * abs(s), "chest": (16, 0, 8 * s), "pelvis": (0, 6 * s, 0),
            "shoulder_l": fwd(12 - 10 * s), "elbow_l": X(-20), "shoulder_r": fwd(8 + 16 * s),
            "head": (0, 0, 7 * sn(t, 0.2))}


def shamble_attack(t):
    rest = {"shoulder_l": fwd(10), "elbow_l": X(-20)}
    heave = {"shoulder_r": (-130, 0, 20), "elbow_r": X(-30), "chest": (4, 0, -24), "_y": 0.04,
             "shoulder_l": fwd(-10)}
    smash = {"shoulder_r": fwd(50), "elbow_r": X(-4), "chest": (30, 0, 18), "_y": -0.10, "_z": -0.03,
             "hip_l": fwd(22), "knee_r": X(14), "shoulder_l": fwd(20)}
    return swing(t, rest, heave, smash, (0.44, 0.62, 1.0))


def shamble_hurt(t):
    return struck(math.sin(t * math.pi), shoulder_r=fwd(-20), shoulder_l=fwd(-26))


def shamble_death(t):
    # It does not fall so much as slump: the peat goes out of it and it sinks
    # into a heap where it stood.
    k = ease(t)
    return {"_z": -0.30 * k, "_pitch": 18 * k, "chest": X(34 * k), "head": X(20 * k),
            "shoulder_l": (0, 0, -40 * k), "shoulder_r": (0, 0, 40 * k), "elbow_l": X(-30 * k),
            "hip_l": fwd(-30 * k), "hip_r": fwd(26 * k), "knee_l": X(60 * k), "knee_r": X(50 * k), "_s": -0.06 * k}


# --- Fen Gator --------------------------------------------------------------------------
def build_fen_gator():
    """Long and low, and mostly jaw: a ridged back in two rows of scutes, legs
    splayed out to the sides, a tail as long as the rest of it, and two eyes on
    top of the head so it can lie under the water with only them showing."""
    r = Rig()
    r.joint("body", (0, 0, 0.15))
    r.add("chest", E(0.18, 0.25, 0.105), "gator", "body", loc=(0, -0.14, 0.0))
    r.add("hips", E(0.17, 0.23, 0.105), "gator", "body", loc=(0, 0.19, 0.0))
    r.add("belly", E(0.16, 0.42, 0.06), "gator_belly", "body", loc=(0, 0.02, -0.055))
    for k in range(8):
        y = -0.32 + k * 0.09
        for sx in (-1, 1):
            r.add("scute", E(0.032, 0.036, 0.028), "gator_dk", "body", loc=(sx * 0.065, y, 0.095))
            r.add("scute", E(0.024, 0.028, 0.02), "gator_dk", "body", loc=(sx * 0.13, y + 0.04, 0.07))
    for sx, side in ((-1, "l"), (1, "r")):
        for tag, y in (("fore", -0.20), ("hind", 0.22)):
            j = tag + "_" + side
            r.joint(j, (sx * 0.15, y, -0.01), "body")
            r.limb("upper", (0, 0, 0), (sx * 0.13, 0, -0.02), 0.05, "gator", j, r_tip=0.04)
            r.joint(j + "_knee", (sx * 0.13, 0, -0.02), j)
            r.limb("lower", (0, 0, 0), (0, -0.02, -0.10), 0.04, "gator_dk", j + "_knee", r_tip=0.032)
            r.add("foot", E(0.055, 0.065, 0.022), "gator_dk", j + "_knee", loc=(sx * 0.015, -0.045, -0.11))
            for k in (-1, 0, 1):
                r.limb("claw", (sx * 0.02 + k * 0.03, -0.08, -0.11), (sx * 0.02 + k * 0.045, -0.12, -0.12), 0.012,
                       "lurk_claw", j + "_knee", r_tip=0.004)
    r.joint("head", (0, -0.37, 0.02), "body", rest=(-4, 0, 0))
    r.add("skull", E(0.14, 0.15, 0.075), "gator", "head", loc=(0, -0.04, 0.02))
    r.add("snout", E(0.09, 0.24, 0.05), "gator", "head", loc=(0, -0.28, 0.01))
    r.add("nose", E(0.045, 0.045, 0.03), "gator_dk", "head", loc=(0, -0.48, 0.045))
    for sx in (-1, 1):
        r.add("brow", E(0.05, 0.055, 0.045), "gator_dk", "head", loc=(sx * 0.075, -0.06, 0.08))
        r.add("eye", E(0.032, 0.03, 0.03), "gator_eye_glow", "head", loc=(sx * 0.075, -0.095, 0.095))
        for k in range(5):
            r.limb("tooth", (sx * 0.08, -0.14 - k * 0.07, -0.015), (sx * 0.085, -0.14 - k * 0.07, -0.06), 0.013,
                   "tooth", "head", r_tip=0.004)
    r.joint("jaw", (0, -0.02, -0.025), "head")
    r.add("jawp", E(0.085, 0.24, 0.03), "gator_belly", "jaw", loc=(0, -0.25, -0.03))
    r.joint("tail1", (0, 0.38, 0.0), "body", rest=(86, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.32), 0.11, "gator", "tail1", r_tip=0.085)
    r.joint("tail2", (0, 0, -0.32), "tail1", rest=(3, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.30), 0.085, "gator", "tail2", r_tip=0.055)
    r.joint("tail3", (0, 0, -0.30), "tail2", rest=(3, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.30), 0.055, "gator_dk", "tail3", r_tip=0.015)
    for j, n, w in (("tail1", 3, 0.03), ("tail2", 3, 0.026), ("tail3", 3, 0.02)):
        for k in range(n):
            r.add("ridge", E(w, 0.03, 0.035), "gator_dk", j, loc=(0, 0.07 - k * 0.012, -0.06 - k * 0.10))
    return r.fit("fen_gator", 44)


def gator_legs(t, amp, body_curl):
    s = sn(t)
    return {"fore_l": (0, 0, -amp * s), "hind_r": (0, 0, -amp * s), "fore_r": (0, 0, -amp * s),
            "hind_l": (0, 0, -amp * s), "fore_l_knee": X(10 * max(0.0, s)), "fore_r_knee": X(10 * max(0.0, -s)),
            "hind_l_knee": X(10 * max(0.0, -s)), "hind_r_knee": X(10 * max(0.0, s)),
            "body": (0, 0, body_curl * s), "head": (0, 0, -body_curl * 0.8 * s),
            "tail1": (0, 0, -body_curl * 1.4 * s), "tail2": (0, 0, -body_curl * 1.6 * sn(t, 0.15)),
            "tail3": (0, 0, -body_curl * 2.0 * sn(t, 0.3))}


def gator_idle(t):
    v = gator_legs(t, 0, 3)
    v["jaw"] = X(3 + 3 * max(0.0, sn(t)))
    return v


def gator_walk(t):
    # The belly off the ground, the legs going round in the diagonal pairs a
    # lizard walks in, and the whole spine bending side to side with them.
    v = gator_legs(t, 22, 9)
    v["_z"] = 0.02
    return v


def gator_attack(t):
    # The head comes up, the jaw drops open, and it throws itself forward and
    # shuts it.
    rest = {}
    gape = {"head": X(-18), "jaw": X(44), "body": X(-6), "_y": 0.04, "tail1": (0, 0, 14)}
    snap = {"head": X(6), "jaw": X(0), "body": X(4), "_y": -0.20, "_z": 0.02, "tail1": (0, 0, -16),
            "fore_l": (0, 0, 18), "fore_r": (0, 0, -18)}
    return swing(t, rest, gape, snap, (0.42, 0.58, 1.0))


def gator_hurt(t):
    k = math.sin(t * math.pi)
    return {"_y": 0.06 * k, "head": (-14 * k, 0, 14 * k), "jaw": X(20 * k), "tail1": (0, 0, 24 * k),
            "body": (0, 0, -8 * k)}


def gator_death(t):
    # Over onto its back, the pale belly up, the legs gone stiff.
    k = ease(t * 1.1)
    return {"_roll": 175 * k, "_z": 0.12 * k * (1 - k) + 0.12 * k, "jaw": X(22 * k),
            "fore_l": X(-30 * k), "fore_r": X(-30 * k), "hind_l": X(30 * k), "hind_r": X(30 * k),
            "tail1": (0, 0, 20 * k), "tail2": (0, 0, 16 * k)}


def gator_swim(t):
    # Under to the eyes: the ridge of the back, the top of the head and the
    # eyes on it are all there is above the water, and the tail sculls.
    s = sn(t)
    return {"_z": -0.14 + 0.006 * s, "fore_l": X(40), "fore_r": X(40), "hind_l": X(50), "hind_r": X(50),
            "body": (0, 0, 3 * s), "head": X(-3), "tail1": (0, 0, -14 * s), "tail2": (0, 0, -20 * sn(t, 0.15)),
            "tail3": (0, 0, -26 * sn(t, 0.3))}


# --- Fen Stalker -----------------------------------------------------------------------
def build_fen_stalker():
    """The thing in the reeds that looks like the reeds: a mantis as tall as a
    man, the colour of dry stems, with its scythes folded in front of it and
    two amber eyes that turn to follow you before anything else of it moves."""
    r = Rig()
    r.joint("body", (0, 0, 0.36))
    r.add("meso", E(0.09, 0.12, 0.08), "mantis", "body")
    r.joint("abdomen", (0, 0.08, 0.0), "body", rest=(-6, 0, 0))
    r.add("abd", E(0.12, 0.31, 0.10), "mantis", "abdomen", loc=(0, 0.28, 0.02))
    for k in range(4):
        r.add("band", E(0.122, 0.02, 0.102), "mantis_dk", "abdomen", loc=(0, 0.12 + k * 0.09, 0.02))
    for sx in (-1, 1):
        r.add("wing", E(0.065, 0.30, 0.022), "mantis_wing", "abdomen", loc=(sx * 0.05, 0.25, 0.11),
              rot=(0.08, 0, sx * 0.08))
    r.joint("chest", (0, -0.06, 0.04), "body", rest=(22, 0, 0))
    r.limb("pro", (0, 0, 0), (0, 0, 0.36), 0.06, "mantis", "chest", r_tip=0.05)
    r.add("collar", E(0.085, 0.075, 0.05), "mantis_dk", "chest", loc=(0, 0, 0.32))
    r.joint("neck", (0, 0, 0.38), "chest", rest=(-34, 0, 0))
    r.joint("head", (0, 0, 0.03), "neck")
    r.add("skull", E(0.10, 0.06, 0.07), "mantis", "head", loc=(0, -0.02, 0.04))
    r.add("face", E(0.05, 0.05, 0.075), "mantis_lt", "head", loc=(0, -0.05, -0.01))
    for sx in (-1, 1):
        r.add("eye", E(0.048, 0.045, 0.052), "mantis_eye_glow", "head", loc=(sx * 0.10, -0.02, 0.06))
        r.limb("antenna", (sx * 0.03, -0.03, 0.09), (sx * 0.12, 0.08, 0.34), 0.012, "mantis_dk", "head",
               r_tip=0.004)
        r.limb("mandible", (sx * 0.02, -0.07, -0.06), (sx * 0.005, -0.09, -0.10), 0.012, "mantis_dk", "head",
               r_tip=0.004)
    # The scythes: a thick thigh held out in front and a spined blade folded
    # back down against it.
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.08, -0.04, 0.30), "chest", rest=(-128, sx * -10, 0))
        r.limb("femur", (0, 0, 0), (0, 0, -0.32), 0.055, "mantis_lt", "shoulder_" + side, r_tip=0.045)
        for k in range(4):
            r.limb("spine", (0, 0.035, -0.08 - k * 0.06), (0, 0.085, -0.11 - k * 0.06), 0.014, "mantis_spine",
                   "shoulder_" + side, r_tip=0.003)
        r.joint("elbow_" + side, (0, 0, -0.32), "shoulder_" + side, rest=(150, 0, 0))
        r.limb("scythe", (0, 0, 0), (0, 0, -0.30), 0.042, "mantis_lt", "elbow_" + side, r_tip=0.01)
        for k in range(3):
            r.limb("tooth", (0, -0.03, -0.08 - k * 0.07), (0, -0.07, -0.10 - k * 0.07), 0.012, "mantis_spine",
                   "elbow_" + side, r_tip=0.003)
        r.limb("hook", (0, 0, -0.28), (0, -0.07, -0.34), 0.018, "mantis_spine", "elbow_" + side, r_tip=0.003)
    # Four walking legs, knees high like the spider's.
    for side in (-1, 1):
        for i, ang in enumerate((-30, 45)):
            name = "leg_%s%d" % ("l" if side < 0 else "r", i)
            r.joint(name, (side * 0.07, 0.02 + i * 0.07, -0.02), "body", rest=(0, 0, side * ang))
            knee = (side * 0.26, 0, 0.12)
            foot = (side * 0.42, 0, -0.36)
            r.limb("thigh", (0, 0, 0), knee, 0.026, "mantis", name, r_tip=0.02)
            r.limb("shin", knee, foot, 0.02, "mantis_dk", name, r_tip=0.01)
    return r.fit("fen_stalker", 36)


def stalk_legs(t, amp, lift):
    out = {}
    for side in ("l", "r"):
        for i in range(2):
            s = sn(t, 0.5 * ((i + (side == "r")) % 2))
            out["leg_%s%d" % (side, i)] = (-lift * max(0.0, s), 0.0, amp * s)
    return out


def stalk_idle(t):
    # It sways like a stem in the wind, which is the whole of its disguise.
    s = sn(t)
    v = stalk_legs(t, 2, 2)
    v.update({"chest": (22 + 3 * s, 0, 4 * sn(t, 0.25)), "head": (0, 0, 10 * sn(t, 0.4)),
              "abdomen": X(2 * s)})
    return v


def stalk_walk(t):
    v = stalk_legs(t, 16, 12)
    v.update({"_z": 0.015 * abs(sn(t * 2)), "chest": (22, 0, 5 * sn(t)), "head": (0, 0, -6 * sn(t))})
    return v


def stalk_attack(t):
    # The strike: both scythes flung out and snapped shut in a single frame.
    rest = {}
    cock = {"chest": (4, 0, 0), "shoulder_l": X(-30), "shoulder_r": X(-30), "elbow_l": X(-60), "elbow_r": X(-60),
            "_y": 0.03, "head": X(-10)}
    strike = {"chest": (40, 0, 0), "shoulder_l": X(50), "shoulder_r": X(50), "elbow_l": X(-120),
              "elbow_r": X(-120), "_y": -0.12, "head": X(14)}
    return swing(t, rest, cock, strike, (0.40, 0.52, 1.0))


def stalk_hurt(t):
    k = math.sin(t * math.pi)
    v = stalk_legs(t, 0, 0)
    v.update({"chest": X(-14 * k), "_y": 0.05 * k, "head": (0, 0, 18 * k), "shoulder_l": X(-20 * k),
              "shoulder_r": X(-20 * k)})
    return v


def stalk_death(t):
    k = ease(t * 1.2)
    v = {"_roll": 170 * k, "_z": 0.2 * k * (1 - k) + 0.20 * k, "chest": X(-30 * k),
         "shoulder_l": X(40 * k), "shoulder_r": X(40 * k)}
    for side in ("l", "r"):
        for i in range(2):
            v["leg_%s%d" % (side, i)] = (-70 * k, 0, 0)
    return v


# --- Drowned One ------------------------------------------------------------------------
def build_drowned_one():
    """Somebody the bayou kept. Swollen and grey-blue, in what is left of a
    shirt, wound about with the anchor chain that took them down and still
    carrying the anchor; hair plastered over the face, and a light in the
    eyes behind it that was never in them alive."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.60))
    r.add("hips", E(0.17, 0.14, 0.12), "drown_rag", "pelvis")
    humanoid_legs(r, -0.04, 0.10, 0.26, 0.26, 0.072, "drown_rag", "drown", foot_col="drown_dk")
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(12, 0, -4))
    r.add("torso", E(0.22, 0.18, 0.26), "drown", "chest", loc=(0, 0, 0.22))
    r.add("shirt", E(0.225, 0.185, 0.15), "drown_rag", "chest", loc=(0, 0, 0.13))
    r.add("chain", TORUS(0.215, 0.024), "iron_rust", "chest", loc=(0, 0, 0.27), rot=(0.25, 0.0, 0.1))
    r.add("chain2", TORUS(0.20, 0.022), "iron_rust_lt", "chest", loc=(0, 0.0, 0.13), rot=(-0.2, 0.1, 0))
    for x, y, z in ((0.14, -0.14, 0.34), (-0.12, -0.15, 0.22), (0.16, 0.10, 0.40)):
        r.add("barnacle", E(0.03, 0.025, 0.025), "barnacle", "chest", loc=(x, y, z))
    for x in (-0.18, 0.18):
        r.limb("weed", (x, 0.02, 0.44), (x * 1.2, -0.05, 0.06), 0.025, "weed", "chest", r_tip=0.01)
    humanoid_arms(r, 0.40, 0.22, 0.24, 0.23, 0.062, "drown", "drown_dk", flare=14)
    for side in ("l", "r"):
        r.add("cuff", E(0.07, 0.07, 0.07), "drown_rag", "shoulder_" + side, loc=(0, 0, -0.08))
        fingers(r, side, "drown_dk", r_=0.016, reach=0.08, spread=0.04)
    r.joint("neck", (0, -0.02, 0.48), "chest", rest=(8, 0, 0))
    r.joint("head", (0, -0.01, 0.08), "neck", rest=(-12, 0, 0))
    r.add("skull", E(0.12, 0.125, 0.13), "drown", "head", loc=(0, 0, 0.08))
    r.add("jaw", E(0.08, 0.08, 0.05), "drown_dk", "head", loc=(0, -0.07, -0.03))
    for sx in (-1, 1):
        r.add("eye", E(0.024, 0.016, 0.02), "drown_eye_glow", "head", loc=(sx * 0.05, -0.118, 0.09))
    r.add("scalp", E(0.125, 0.13, 0.08), "drown_hair", "head", loc=(0, 0.01, 0.14))
    for x in (-0.09, -0.03, 0.035, 0.095):
        r.limb("strand", (x, -0.08, 0.18), (x * 1.15, -0.135, -0.02 - abs(x) * 0.6), 0.02, "drown_hair", "head",
               r_tip=0.008)
    for x in (-0.11, 0.11, 0.0):
        r.limb("strand", (x, 0.04, 0.16), (x * 1.3, 0.10, -0.12), 0.025, "drown_hair", "head", r_tip=0.01)
    # The anchor that took them down, dragged by its ring, flukes to the ground.
    r.add("ring", TORUS(0.05, 0.016), "iron_rust_lt", "hand_r", loc=(0, 0, -0.02), rot=(0, 1.57, 0))
    r.add("stock", E(0.15, 0.024, 0.024), "iron_rust_lt", "hand_r", loc=(0, 0, -0.08))
    r.limb("shank", (0, 0, -0.06), (0, 0, -0.46), 0.034, "iron_rust", "hand_r", r_tip=0.03)
    r.add("crown", E(0.055, 0.045, 0.045), "iron_rust", "hand_r", loc=(0, 0, -0.46))
    for sx in (-1, 1):
        r.limb("arm", (0, 0, -0.46), (sx * 0.15, -0.02, -0.38), 0.034, "iron_rust", "hand_r", r_tip=0.028)
        r.limb("fluke", (sx * 0.15, -0.02, -0.38), (sx * 0.19, -0.03, -0.26), 0.04, "iron_rust_lt", "hand_r",
               r_tip=0.008)
    return r.fit("drowned_one", 32, DROWN_REST)


DROWN_REST = {"shoulder_r": fwd(6), "elbow_r": X(-4), "hand_r": X(-6), "shoulder_l": fwd(40), "elbow_l": X(-30)}


def drowned_idle(t):
    s = sn(t)
    v = dict(DROWN_REST)
    v.update({"_z": 0.006 * s, "chest": (12 + 2 * s, 0, -4), "head": (0, 0, 6 * sn(t, 0.3)),
              "shoulder_l": fwd(40 + 5 * s)})
    return v


def drowned_walk(t):
    # Heavy with water: every step lands flat and the body sags after it.
    v = gait(t, 24, 22, 0, 0.03, 12)
    v.update(DROWN_REST)
    v.update({"shoulder_l": fwd(44 - 8 * sn(t)), "shoulder_r": fwd(14 + 6 * sn(t)),
              "head": (0, 0, 8 * sn(t, 0.2)), "pelvis": (0, 5 * sn(t), 0)})
    return v


def drowned_attack(t):
    raise_ = {"shoulder_r": fwd(-120), "elbow_r": X(-40), "hand_r": X(20), "chest": (-4, 0, -18), "_y": 0.03,
              "shoulder_l": fwd(20)}
    down = {"shoulder_r": fwd(62), "elbow_r": X(-8), "hand_r": X(20), "chest": (22, 0, 18), "_y": -0.10,
            "hip_l": fwd(24), "hip_r": fwd(-14), "shoulder_l": fwd(10)}
    return swing(t, DROWN_REST, raise_, down)


def drowned_hurt(t):
    v = dict(DROWN_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=fwd(-30)))
    return v


def drowned_death(t):
    return topple(t, armed=True)


def drowned_swim(t):
    # Chest-deep and wading, the anchor dragged under the surface.
    s = sn(t)
    v = dict(DROWN_REST)
    v.update({"_z": -0.40 + 0.01 * s, "hip_l": fwd(20 * s), "hip_r": fwd(-20 * s), "chest": (16, 0, 4 * s),
              "shoulder_l": fwd(30 + 14 * s), "shoulder_r": fwd(-10), "elbow_r": X(-20),
              "head": (0, 0, 6 * sn(t, 0.25))})
    return v


# --- Witchlight -------------------------------------------------------------------------
def build_witchlight():
    """A light over the water where no one is holding one. A flame of green
    with a white heart and a face in it -- two hollows and a mouth -- and three
    motes going round it that it throws when it means to."""
    r = Rig()
    r.joint("body", (0, 0, 0.56))
    r.add("halo", E(0.20, 0.19, 0.22), "witch", "body")
    r.add("flame", E(0.15, 0.15, 0.18), "witch_lt", "body", loc=(0, -0.04, 0.03))
    r.add("core", E(0.09, 0.09, 0.10), "witch_glow", "body", loc=(0, -0.09, 0.02))
    for k, (x, y, h) in enumerate(((0.0, 0.02, 0.34), (-0.10, 0.04, 0.24), (0.10, 0.0, 0.26),
                                   (-0.05, 0.10, 0.22), (0.06, 0.08, 0.28))):
        r.limb("tongue", (x, y, 0.08), (x * 1.4, y + 0.06, 0.08 + h), 0.075, "witch_lt" if k % 2 else "witch",
               "body", r_tip=0.012)
    r.joint("tail1", (0, 0.02, -0.12), "body", rest=(24, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.24), 0.10, "witch", "tail1", r_tip=0.025)
    for sx in (-1, 1):
        r.add("hollow", E(0.032, 0.02, 0.048), "witch_eye", "body", loc=(sx * 0.065, -0.185, 0.06))
    r.add("mouth", E(0.045, 0.02, 0.022), "witch_eye", "body", loc=(0, -0.19, -0.04))
    r.joint("orbit", (0, 0, 0.0), "body")
    for k in range(3):
        a = k * math.tau / 3
        r.add("mote", E(0.035, 0.035, 0.035), "witch_glow", "orbit",
              loc=(0.32 * math.cos(a), 0.32 * math.sin(a), 0.05 * math.sin(2 * a)))
    return r.fit("witchlight", 24)


def wisp_idle(t):
    s = sn(t)
    return {"_z": 0.04 * s, "orbit": (0, 0, 360 * t / 3), "body": (4 * s, 0, 6 * sn(t, 0.25)),
            "tail1": (0, 0, 14 * sn(t, 0.2)), "_s": 0.04 * sn(t * 2)}


def wisp_walk(t):
    v = wisp_idle(t)
    v.update({"body": (14, 0, 6 * sn(t, 0.25)), "tail1": (18, 0, 20 * sn(t, 0.2))})
    return v


def wisp_attack(t):
    # It flares -- twice its size and bright -- and lets go.
    i, k = phases(t, 0.45, 0.60, 1.0)
    flare = {"_s": 0.40, "_z": 0.06, "orbit": (0, 0, 60), "body": X(-10)}
    throw = {"_s": -0.10, "_y": -0.08, "orbit": (0, 0, 140), "body": X(20)}
    return [mix({}, flare, k), mix(flare, throw, k), mix(throw, {}, k), {}][i]


def wisp_hurt(t):
    k = math.sin(t * math.pi)
    return {"_s": -0.25 * k, "_y": 0.06 * k, "body": (-20 * k, 0, 20 * k)}


def wisp_death(t):
    # It gutters out: shrinks, drops towards the water, and is gone.
    k = ease(t)
    return {"_s": -0.96 * k, "_z": -0.30 * k, "orbit": (0, 0, 200 * k), "body": X(30 * k)}


# --- Lizard Shaman ------------------------------------------------------------------------
def build_lizard_shaman():
    """The lizardmen's wise one: leaner than the warriors, in a hide robe painted
    with red bands, feathers standing up off the shoulders and head, a string
    of teeth round the neck and a bird's skull worn over the snout. The staff
    holds a light caught in three prongs of wood."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.70))
    r.add("hips", E(0.20, 0.16, 0.13), "shaman", "pelvis")
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.12, 0, -0.04), "pelvis")
        r.limb("thigh", (0, 0, 0), (0, -0.02, -0.32), 0.09, "shaman", "hip_" + side, r_tip=0.07)
        r.joint("knee_" + side, (0, -0.02, -0.32), "hip_" + side, rest=(-30, 0, 0))
        r.limb("shin", (0, 0, 0), (0, 0, -0.34), 0.06, "shaman_dk", "knee_" + side, r_tip=0.045)
        r.add("foot", E(0.07, 0.12, 0.045), "shaman_dk", "knee_" + side, loc=(0, -0.06, -0.31))
        for k in (-1, 0, 1):
            r.limb("claw", (k * 0.04, -0.11, -0.32), (k * 0.06, -0.17, -0.33), 0.018, "liz_claw",
                   "knee_" + side, r_tip=0.004)
    # The robe: hide, to the shin, with the red bands painted round it.
    r.add("robe", C(0.19, 0.25, 0.30, squash_y=0.85), "shaman_robe", "pelvis", loc=(0, 0.01, 0.02))
    r.add("band", TORUS(0.235, 0.022), "paint_red", "pelvis", loc=(0, 0.01, -0.34), rot=(0, 0, 0))
    r.add("band2", TORUS(0.21, 0.018), "paint_red", "pelvis", loc=(0, 0.01, -0.16), rot=(0, 0, 0))
    r.add("gourd", E(0.05, 0.05, 0.07), "reed", "pelvis", loc=(0.21, -0.06, -0.02))
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(8, 0, 0))
    r.add("torso", E(0.23, 0.17, 0.26), "shaman", "chest", loc=(0, 0, 0.22))
    for k in range(3):
        r.add("plate", E(0.12 - k * 0.012, 0.05, 0.05), "liz_belly", "chest", loc=(0, -0.13, 0.10 + k * 0.10))
    r.add("mantle", E(0.25, 0.19, 0.08), "shaman_robe_dk", "chest", loc=(0, 0.02, 0.38))
    for k in range(7):
        a = math.radians(-60 + k * 20)
        col = ("feather_r", "feather_w", "feather_b")[k % 3]
        r.limb("feather", (math.sin(a) * 0.18, 0.08, 0.38), (math.sin(a) * 0.34, 0.20, 0.62 - abs(k - 3) * 0.04),
               0.03, col, "chest", r_tip=0.008)
    for k in range(7):
        a = math.radians(-60 + k * 20)
        r.add("tooth", E(0.018, 0.015, 0.03), "bone_w" if k % 2 else "tooth", "chest",
              loc=(math.sin(a) * 0.13, -0.15 + abs(math.sin(a)) * 0.05, 0.36 - math.cos(a) * 0.08))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.24, 0, 0.38), "chest", rest=(0, sx * -12, 0))
        r.limb("upper", (0, 0, 0), (0, 0, -0.25), 0.07, "shaman", "shoulder_" + side, r_tip=0.06)
        r.joint("elbow_" + side, (0, 0, -0.25), "shoulder_" + side, rest=(-18, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.24), 0.06, "shaman_dk", "elbow_" + side, r_tip=0.048)
        r.add("bangle", TORUS(0.055, 0.014), "fen_mark", "elbow_" + side, loc=(0, 0, -0.16))
        r.joint("hand_" + side, (0, 0, -0.24), "elbow_" + side)
        r.add("hand", E(0.06, 0.055, 0.065), "shaman_dk", "hand_" + side)
        fingers(r, side, "liz_claw", r_=0.015, reach=0.08, spread=0.045)
    r.joint("neck", (0, -0.02, 0.52), "chest", rest=(2, 0, 0))
    r.limb("neckp", (0, 0, 0), (0, 0, 0.18), 0.10, "shaman", "neck", r_tip=0.09)
    r.joint("head", (0, -0.01, 0.19), "neck", rest=(-4, 0, 0))
    r.add("skull", E(0.14, 0.15, 0.13), "shaman", "head", loc=(0, 0, 0.05))
    r.add("snout", E(0.10, 0.16, 0.085), "shaman", "head", loc=(0, -0.19, 0.01))
    r.add("jaw", E(0.09, 0.15, 0.05), "liz_belly", "head", loc=(0, -0.18, -0.07))
    # A bird's skull worn over the snout as a mask.
    r.add("mask", E(0.11, 0.14, 0.06), "bone_w", "head", loc=(0, -0.16, 0.08))
    r.limb("beak", (0, -0.26, 0.08), (0, -0.40, 0.02), 0.04, "bone_w", "head", r_tip=0.01)
    for sx in (-1, 1):
        r.add("hole", E(0.035, 0.02, 0.03), "shroud_dk", "head", loc=(sx * 0.06, -0.24, 0.09))
        r.add("eye", E(0.022, 0.012, 0.018), "shaman_eye_glow", "head", loc=(sx * 0.06, -0.255, 0.09))
    # The headdress: a fan of long feathers standing up behind the head.
    for k in range(5):
        a = math.radians(-40 + k * 20)
        col = ("feather_b", "feather_w", "feather_r", "feather_w", "feather_b")[k]
        r.limb("plume", (math.sin(a) * 0.06, 0.06, 0.14), (math.sin(a) * 0.20, 0.16, 0.46 - abs(k - 2) * 0.05),
               0.032, col, "head", r_tip=0.008)
    r.add("headband", TORUS(0.13, 0.022), "paint_red", "head", loc=(0, 0.0, 0.12), rot=(0.2, 0, 0))
    # The tail, under the robe's hem at the back.
    r.joint("tail1", (0, 0.16, -0.08), "pelvis", rest=(60, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.28), 0.11, "shaman", "tail1", r_tip=0.08)
    r.joint("tail2", (0, 0, -0.28), "tail1", rest=(16, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.26), 0.08, "shaman", "tail2", r_tip=0.05)
    r.joint("tail3", (0, 0, -0.26), "tail2", rest=(14, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.22), 0.05, "shaman_dk", "tail3", r_tip=0.015)
    # The staff: a light caught in three prongs, with a feather and a skull hung
    # off it.
    r.limb("staff", (0, 0, 0.92), (0, 0, -0.70), 0.03, "driftwood", "hand_r", r_tip=0.026)
    for k in range(3):
        a = k * math.tau / 3
        r.limb("prong", (0, 0, 0.90), (math.cos(a) * 0.08, math.sin(a) * 0.08, 1.12), 0.022, "driftwood_dk",
               "hand_r", r_tip=0.008)
    r.add("orb", E(0.075, 0.075, 0.075), "shaman_glow", "hand_r", loc=(0, 0, 1.03))
    r.limb("hangfeather", (0.03, 0, 0.88), (0.07, -0.02, 0.68), 0.02, "feather_r", "hand_r", r_tip=0.006)
    r.add("smallskull", E(0.04, 0.04, 0.045), "bone_w", "hand_r", loc=(-0.05, -0.03, 0.80))
    return r.fit("lizard_shaman", 60, SHAMAN_REST)


# Straight up: 8 (chest) - 8 (shoulder) - 18 - 22 (elbow) + 40 = 0.
SHAMAN_REST = {"shoulder_r": fwd(8), "elbow_r": X(-22), "hand_r": X(40), "shoulder_l": fwd(10), "elbow_l": X(-30)}


def shaman_idle(t):
    s = sn(t)
    v = dict(SHAMAN_REST)
    v.update({"_z": 0.005 * s, "chest": X(2 * s), "tail1": (0, 0, 8 * s), "tail2": (0, 0, 10 * sn(t, 0.2)),
              "tail3": (0, 0, 12 * sn(t, 0.35)), "head": X(3 * sn(t * 2)), "shoulder_l": fwd(10 + 6 * s)})
    return v


def shaman_walk(t):
    v = gait(t, 30, 24, 16, 0.025, 6)
    v.update(SHAMAN_REST)
    v.update({"tail1": (0, 0, 16 * sn(t)), "tail2": (0, 0, 20 * sn(t, 0.25)), "tail3": (0, 0, 24 * sn(t, 0.4)),
              "shoulder_r": fwd(8 + 6 * sn(t)), "shoulder_l": fwd(10 - 16 * sn(t))})
    return v


def shaman_attack(t):
    # The staff held up to the sky, and then its light pointed at you.
    raise_ = {"shoulder_r": fwd(-160), "elbow_r": X(-10), "hand_r": X(20), "chest": X(-12), "head": X(-12),
              "shoulder_l": (0, 60, 0), "elbow_l": X(-20), "_z": 0.02, "tail1": (0, 0, -10)}
    point = {"shoulder_r": fwd(78), "elbow_r": X(-6), "hand_r": X(20), "chest": X(16), "head": X(8),
             "shoulder_l": fwd(40), "_y": -0.06, "hip_l": fwd(22), "hip_r": fwd(-14), "tail1": (-8, 0, 10)}
    return swing(t, SHAMAN_REST, raise_, point, (0.44, 0.62, 1.0))


def shaman_hurt(t):
    v = dict(SHAMAN_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=fwd(-30)))
    return v


def shaman_death(t):
    v = topple(t, armed=True)
    v.update({"tail1": X(26 * ease(t)), "tail2": X(16 * ease(t))})
    return v


# --- The Mother of the Fen ----------------------------------------------------------------
def build_bayou_matriarch():
    """The Bayou's queen: a lizardwoman grown old and vast into something nearer
    a crocodile, with the long jaw to go with it. A crown of bone and reed, a
    mantle of moss and hanging shells, orange markings down a deep green hide,
    and a driftwood staff topped with a gator's skull and a green stone that
    lights when she calls the water up."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.66))
    r.add("hips", E(0.31, 0.25, 0.19), "fen", "pelvis")
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.19, 0, -0.05), "pelvis")
        r.limb("thigh", (0, 0, 0), (0, -0.02, -0.30), 0.14, "fen", "hip_" + side, r_tip=0.11)
        r.joint("knee_" + side, (0, -0.02, -0.30), "hip_" + side, rest=(-26, 0, 0))
        r.limb("shin", (0, 0, 0), (0, 0, -0.32), 0.10, "fen_dk", "knee_" + side, r_tip=0.08)
        r.add("foot", E(0.11, 0.17, 0.06), "fen_dk", "knee_" + side, loc=(0, -0.08, -0.30))
        for k in (-1, 0, 1):
            r.limb("claw", (k * 0.06, -0.16, -0.31), (k * 0.085, -0.25, -0.33), 0.026, "liz_claw",
                   "knee_" + side, r_tip=0.006)
    r.add("skirt", C(0.26, 0.30, 0.18, squash_y=0.85), "moss", "pelvis", loc=(0, 0.01, 0.02))
    for k in range(6):
        r.limb("reedskirt", (-0.26 + k * 0.105, -0.16, -0.06), (-0.30 + k * 0.12, -0.22, -0.42), 0.03,
               "reed" if k % 2 else "weed", "pelvis", r_tip=0.01)
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(12, 0, 0))
    r.add("torso", E(0.37, 0.27, 0.34), "fen", "chest", loc=(0, 0, 0.28))
    r.add("back", E(0.33, 0.17, 0.25), "fen_lt", "chest", loc=(0, 0.08, 0.32))
    for k in range(4):
        r.add("plate", E(0.18 - k * 0.015, 0.06, 0.065), "fen_belly", "chest", loc=(0, -0.19, 0.10 + k * 0.12))
    for x, z in ((-0.30, 0.20), (0.31, 0.28), (-0.28, 0.40)):
        r.add("mark", E(0.03, 0.05, 0.04), "fen_mark", "chest", loc=(x, -0.06, z))
    for k in range(5):
        r.limb("ridge", (0, 0.20, 0.52 - k * 0.11), (0, 0.28, 0.58 - k * 0.11), 0.04, "fen_dk", "chest",
               r_tip=0.008)
    # A mantle of moss over the shoulders, with shells strung on it.
    r.add("mantle", E(0.40, 0.28, 0.12), "moss", "chest", loc=(0, 0.03, 0.50))
    for k in range(5):
        a = math.radians(-70 + k * 35)
        r.add("shell", E(0.035, 0.02, 0.04), "barnacle" if k % 2 else "bone_w", "chest",
              loc=(math.sin(a) * 0.30, -0.18 - 0.04 * math.cos(a), 0.44 - math.cos(a) * 0.04))
    for sx in (-1, 1):
        for k in range(3):
            r.limb("reed", (sx * (0.26 + k * 0.05), 0.12, 0.52), (sx * (0.36 + k * 0.08), 0.26, 0.86 - k * 0.06),
                   0.022, "reed", "chest", r_tip=0.008)
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.36, 0, 0.46), "chest", rest=(0, sx * -14, 0))
        r.add("deltoid", E(0.13, 0.13, 0.12), "fen_lt", "shoulder_" + side)
        r.limb("upper", (0, 0, 0), (0, 0, -0.30), 0.11, "fen", "shoulder_" + side, r_tip=0.09)
        r.joint("elbow_" + side, (0, 0, -0.30), "shoulder_" + side, rest=(-18, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.28), 0.09, "fen_dk", "elbow_" + side, r_tip=0.075)
        r.add("bangle", TORUS(0.085, 0.02), "gold", "elbow_" + side, loc=(0, 0, -0.18))
        r.joint("hand_" + side, (0, 0, -0.28), "elbow_" + side)
        r.add("hand", E(0.09, 0.085, 0.09), "fen_dk", "hand_" + side)
        fingers(r, side, "liz_claw", r_=0.02, reach=0.11, spread=0.06)
    r.joint("neck", (0, -0.04, 0.60), "chest", rest=(4, 0, 0))
    r.limb("neckp", (0, 0, 0), (0, 0, 0.20), 0.15, "fen", "neck", r_tip=0.13)
    r.joint("head", (0, -0.02, 0.20), "neck", rest=(10, 0, 0))
    r.add("skull", E(0.18, 0.19, 0.15), "fen", "head", loc=(0, 0, 0.05))
    r.add("brow", E(0.17, 0.10, 0.05), "fen_lt", "head", loc=(0, -0.10, 0.14))
    r.add("snout", E(0.12, 0.30, 0.08), "fen", "head", loc=(0, -0.32, 0.0))
    r.add("nose", E(0.06, 0.05, 0.04), "fen_dk", "head", loc=(0, -0.60, 0.05))
    r.joint("jaw", (0, -0.04, -0.05), "head")
    r.add("jawp", E(0.11, 0.30, 0.045), "fen_belly", "jaw", loc=(0, -0.28, -0.04))
    for sx in (-1, 1):
        r.add("eyelid", E(0.06, 0.055, 0.05), "fen_dk", "head", loc=(sx * 0.11, -0.10, 0.13))
        r.add("eye", E(0.05, 0.04, 0.045), "fen_eye_glow", "head", loc=(sx * 0.11, -0.14, 0.145))
        for k in range(5):
            r.limb("tooth", (sx * 0.10, -0.18 - k * 0.08, -0.03), (sx * 0.105, -0.18 - k * 0.08, -0.09), 0.016,
                   "tooth", "head", r_tip=0.004)
        r.add("markh", E(0.04, 0.06, 0.02), "fen_mark", "head", loc=(sx * 0.08, -0.30, 0.07))
    # The crown: spikes of bone and reed round the top of the head, and the
    # green stone in front.
    for k in range(7):
        a = math.radians(-90 + k * 30)
        col = "bone_w" if k % 2 == 0 else "reed"
        r.limb("crown", (math.sin(a) * 0.14, 0.02 - math.cos(a) * 0.12, 0.14),
               (math.sin(a) * 0.20, 0.04 - math.cos(a) * 0.16, 0.40 - abs(k - 3) * 0.035), 0.03, col, "head",
               r_tip=0.006)
    r.add("circlet", TORUS(0.155, 0.025), "gold", "head", loc=(0, 0.0, 0.15), rot=(0.15, 0, 0))
    r.add("stone", E(0.04, 0.03, 0.045), "pearl_glow", "head", loc=(0, -0.15, 0.20))
    r.joint("tail1", (0, 0.20, -0.08), "pelvis", rest=(64, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.36), 0.17, "fen", "tail1", r_tip=0.13)
    r.joint("tail2", (0, 0, -0.36), "tail1", rest=(14, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.32), 0.13, "fen", "tail2", r_tip=0.08)
    r.joint("tail3", (0, 0, -0.32), "tail2", rest=(12, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.30), 0.08, "fen_dk", "tail3", r_tip=0.02)
    for j, n in (("tail1", 3), ("tail2", 3)):
        for k in range(n):
            r.add("ridge", E(0.04, 0.04, 0.04), "fen_dk", j, loc=(0, 0.12, -0.06 - k * 0.11))
    # The staff: driftwood, a gator's skull lashed to the top of it and the
    # stone set in its brow.
    r.limb("staff", (0, 0, 0.96), (0, 0, -0.70), 0.038, "driftwood", "hand_r", r_tip=0.032)
    r.add("gatorskull", E(0.08, 0.15, 0.055), "bone_w", "hand_r", loc=(0, -0.06, 1.02))
    r.add("gatorsnout", E(0.05, 0.13, 0.04), "bone_w", "hand_r", loc=(0, -0.22, 1.00))
    r.add("staffstone", E(0.045, 0.04, 0.045), "pearl_glow", "hand_r", loc=(0, -0.05, 1.09))
    for k in range(2):
        r.limb("lash", (0, 0, 0.92 - k * 0.05), (0.02, -0.02, 0.72 - k * 0.08), 0.016, "reed", "hand_r",
               r_tip=0.006)
    return r.fit("bayou_matriarch", 78, MOTHER_REST)


# Straight up: 12 (chest) - 10 (shoulder) - 18 - 24 (elbow) + 40 = 0.
MOTHER_REST = {"shoulder_r": fwd(10), "elbow_r": X(-24), "hand_r": X(40), "shoulder_l": fwd(14), "elbow_l": X(-34)}


def mother_idle(t):
    s = sn(t)
    v = dict(MOTHER_REST)
    v.update({"_z": 0.005 * s, "chest": X(2 * s), "tail1": (0, 0, 7 * s), "tail2": (0, 0, 9 * sn(t, 0.2)),
              "tail3": (0, 0, 11 * sn(t, 0.35)), "head": (0, 0, 5 * sn(t, 0.3)), "jaw": X(3 + 3 * max(0.0, s))})
    return v


def mother_walk(t):
    v = gait(t, 26, 22, 14, 0.02, 8)
    v.update(MOTHER_REST)
    v.update({"tail1": (0, 0, 14 * sn(t)), "tail2": (0, 0, 18 * sn(t, 0.25)), "tail3": (0, 0, 22 * sn(t, 0.4)),
              "pelvis": (0, 5 * sn(t), 0), "shoulder_l": fwd(14 - 12 * sn(t)), "head": (0, 0, -5 * sn(t))})
    return v


def mother_attack(t):
    # The staff raised in both hands and brought down like a felled tree, the
    # jaw open over it.
    raise_ = {"shoulder_r": (-160, -10, 0), "elbow_r": X(-30), "hand_r": X(10), "shoulder_l": (-150, 10, 0),
              "elbow_l": X(-40), "chest": X(-14), "jaw": X(30), "head": X(-12), "_y": 0.03,
              "tail1": (0, 0, -12)}
    smash = {"shoulder_r": fwd(50), "elbow_r": X(-6), "hand_r": X(10), "shoulder_l": fwd(46), "elbow_l": X(-10),
             "chest": X(26), "jaw": X(8), "head": X(12), "_y": -0.10, "_z": -0.03, "hip_l": fwd(30),
             "hip_r": fwd(-18), "knee_l": X(20), "tail1": (-10, 0, 14)}
    return swing(t, MOTHER_REST, raise_, smash, (0.46, 0.62, 1.0))


def mother_hurt(t):
    v = dict(MOTHER_REST)
    v.update(struck(math.sin(t * math.pi), jaw=X(24), shoulder_l=fwd(-30)))
    return v


def mother_death(t):
    v = topple(t, dir=-1, armed=True)
    v.update({"tail1": X(24 * ease(t)), "tail2": X(14 * ease(t)), "jaw": X(26 * ease(t))})
    return v


# =================================================================================
#  Hollowrest Crypt
# =================================================================================

# --- Grave Ghoul ------------------------------------------------------------------------
def build_grave_ghoul():
    """What eats in a crypt: bent low on long legs, arms longer still, a bald
    head with ears like a bat's and a mouth that opens further than a mouth
    should. It moves on its knuckles when it is in a hurry."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.42))
    r.add("hips", E(0.14, 0.11, 0.10), "ghoul", "pelvis")
    r.add("rag", E(0.14, 0.11, 0.09), "rot_cloth_dk", "pelvis", loc=(0, -0.01, -0.06))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.10, 0, -0.03), "pelvis", rest=(-40, 0, sx * 6))
        r.limb("thigh", (0, 0, 0), (0, 0, -0.24), 0.052, "ghoul", "hip_" + side, r_tip=0.04)
        r.joint("knee_" + side, (0, 0, -0.24), "hip_" + side, rest=(78, 0, 0))
        r.limb("shin", (0, 0, 0), (0, 0, -0.26), 0.04, "ghoul_dk", "knee_" + side, r_tip=0.03)
        r.add("foot", E(0.05, 0.10, 0.03), "ghoul_dk", "knee_" + side, loc=(0, -0.06, -0.26))
        for k in (-1, 0, 1):
            r.limb("toe", (k * 0.03, -0.12, -0.27), (k * 0.045, -0.18, -0.28), 0.014, "ghoul_claw",
                   "knee_" + side, r_tip=0.004)
    r.joint("chest", (0, 0, 0.06), "pelvis", rest=(44, 0, 0))
    r.add("torso", E(0.16, 0.12, 0.22), "ghoul", "chest", loc=(0, 0, 0.18))
    r.add("gut", E(0.11, 0.08, 0.10), "ghoul_dk", "chest", loc=(0, -0.05, 0.04))
    for k in range(4):
        r.add("rib", E(0.12 - k * 0.01, 0.02, 0.014), "ghoul_lt", "chest", loc=(0, -0.10, 0.12 + k * 0.05))
        r.add("vert", E(0.03, 0.03, 0.025), "ghoul_dk", "chest", loc=(0, 0.11, 0.08 + k * 0.08))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.17, 0, 0.32), "chest", rest=(-40, sx * -12, 0))
        r.limb("upper", (0, 0, 0), (0, 0, -0.28), 0.045, "ghoul", "shoulder_" + side, r_tip=0.036)
        r.joint("elbow_" + side, (0, 0, -0.28), "shoulder_" + side, rest=(-10, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.28), 0.038, "ghoul_dk", "elbow_" + side, r_tip=0.03)
        r.joint("hand_" + side, (0, 0, -0.28), "elbow_" + side)
        r.add("hand", E(0.045, 0.04, 0.05), "ghoul_dk", "hand_" + side)
        fingers(r, side, "ghoul_claw", n=3, r_=0.016, reach=0.12, spread=0.04, down=0.08)
    r.joint("neck", (0, -0.03, 0.38), "chest", rest=(-40, 0, 0))
    r.joint("head", (0, -0.03, 0.05), "neck", rest=(-4, 0, 0))
    r.add("skull", E(0.10, 0.12, 0.10), "ghoul", "head", loc=(0, 0.02, 0.06))
    r.add("brow", E(0.09, 0.05, 0.03), "ghoul_dk", "head", loc=(0, -0.08, 0.10))
    r.add("maw", E(0.08, 0.06, 0.06), "ghoul_mouth", "head", loc=(0, -0.09, -0.01))
    r.joint("jaw", (0, -0.01, -0.03), "head")
    r.add("jawp", E(0.075, 0.08, 0.035), "ghoul", "jaw", loc=(0, -0.06, -0.03))
    for sx in (-1, 1):
        r.add("eye", E(0.026, 0.016, 0.022), "ghoul_eye_glow", "head", loc=(sx * 0.045, -0.105, 0.08))
        r.limb("ear", (sx * 0.08, 0.0, 0.08), (sx * 0.20, 0.06, 0.16), 0.035, "ghoul", "head", r_tip=0.006)
        for k in range(2):
            r.limb("fang", (sx * (0.02 + k * 0.03), -0.12, 0.02), (sx * (0.02 + k * 0.03), -0.13, -0.04), 0.012,
                   "tooth", "head", r_tip=0.003)
    return r.fit("grave_ghoul", 28)


def ghoul_idle(t):
    s = sn(t)
    return {"_z": 0.01 * s, "chest": X(2 * s), "head": (0, 0, 14 * sn(t, 0.3)), "jaw": X(6 + 6 * max(0.0, s)),
            "shoulder_l": fwd(8 + 4 * s), "shoulder_r": fwd(8 - 4 * s), "elbow_l": X(-20), "elbow_r": X(-20)}


def ghoul_walk(t):
    # A lope: legs and long arms going together, nearly on all fours.
    s = sn(t)
    return {"hip_l": fwd(26 * s), "hip_r": fwd(-26 * s), "knee_l": X(24 * max(0.0, -s)),
            "knee_r": X(24 * max(0.0, s)), "shoulder_l": fwd(20 - 34 * s), "shoulder_r": fwd(20 + 34 * s),
            "elbow_l": X(-10), "elbow_r": X(-10), "_z": 0.03 * abs(s), "chest": (10, 0, 6 * s),
            "head": (0, 0, -6 * s), "jaw": X(8)}


def ghoul_attack(t):
    # It gathers and springs, both hands at the face in front of it.
    crouch = {"chest": X(10), "_z": -0.05, "hip_l": fwd(20), "hip_r": fwd(20), "knee_l": X(30), "knee_r": X(30),
              "shoulder_l": fwd(-60), "shoulder_r": fwd(-60), "elbow_l": X(-60), "elbow_r": X(-60), "jaw": X(30)}
    spring = {"chest": X(-14), "_z": 0.06, "_y": -0.16, "hip_l": fwd(-20), "hip_r": fwd(-10),
              "shoulder_l": fwd(90), "shoulder_r": fwd(90), "elbow_l": X(-4), "elbow_r": X(-4), "jaw": X(34)}
    return swing(t, {}, crouch, spring, (0.40, 0.56, 1.0))


def ghoul_hurt(t):
    return struck(math.sin(t * math.pi), jaw=X(26), shoulder_l=fwd(-30), shoulder_r=fwd(-30))


def ghoul_death(t):
    v = topple(t)
    v["jaw"] = X(30 * ease(t))
    return v


# --- the skeletons' shared frame ---------------------------------------------------------
def _bones(r, eye="rot_eye_glow"):
    """The skeleton of blender_creatures, as a frame to hang kit on."""
    r.joint("pelvis", (0, 0, 0.60))
    r.add("hips", E(0.13, 0.10, 0.09), "bone_g", "pelvis")
    humanoid_legs(r, -0.03, 0.085, 0.27, 0.27, 0.042, "bone_w", "bone_g", foot_col="bone_g")
    r.joint("chest", (0, 0, 0.07), "pelvis", rest=(6, 0, 0))
    r.limb("spine", (0, 0.02, 0), (0, 0.02, 0.34), 0.035, "bone_g", "chest", r_tip=0.03)
    for k in range(4):
        wide = 0.145 - abs(k - 1) * 0.015
        r.add("rib", E(wide, 0.075, 0.022), "bone_w", "chest", loc=(0, -0.01, 0.08 + k * 0.075))
    r.add("sternum", E(0.035, 0.05, 0.14), "bone_w", "chest", loc=(0, -0.07, 0.17))
    r.add("collar", E(0.16, 0.05, 0.03), "bone_w", "chest", loc=(0, -0.02, 0.36))
    humanoid_arms(r, 0.36, 0.17, 0.23, 0.22, 0.035, "bone_w", "bone_g", flare=14)
    for side in ("l", "r"):
        fingers(r, side, "bone_w", r_=0.011, reach=0.07, spread=0.035, down=0.03)
    r.joint("neck", (0, 0, 0.42), "chest")
    r.limb("vertebra", (0, 0.01, 0), (0, 0.01, 0.07), 0.028, "bone_g", "neck", r_tip=0.026)
    r.joint("head", (0, 0, 0.08), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.10, 0.105, 0.105), "bone_w", "head", loc=(0, 0, 0.07))
    r.add("jaw", E(0.075, 0.075, 0.035), "bone_w", "head", loc=(0, -0.05, -0.01))
    for sx in (-1, 1):
        r.add("socket", E(0.032, 0.02, 0.032), "bone_dk", "head", loc=(sx * 0.045, -0.085, 0.08))
        r.add("spark", E(0.016, 0.012, 0.016), eye, "head", loc=(sx * 0.045, -0.095, 0.08))


# --- Bone Archer -----------------------------------------------------------------------
def build_bone_archer():
    """A skeleton in the rags of an archer's hood and cape, a longbow in its
    left hand and a quiver of black-fletched arrows over its shoulder."""
    r = Rig()
    _bones(r, eye="wisp_glow")
    r.add("hood", E(0.125, 0.13, 0.12), "archer_hood", "head", loc=(0, 0.02, 0.10))
    r.add("hood_peak", E(0.06, 0.08, 0.07), "archer_hood", "head", loc=(0, 0.08, 0.18))
    r.add("cowl", E(0.19, 0.15, 0.08), "archer_hood", "chest", loc=(0, 0.01, 0.37))
    for k in range(3):
        r.add("cape", E(0.07, 0.03, 0.12), "archer_hood_dk", "chest", loc=(-0.10 + k * 0.10, 0.10, 0.22 - (k % 2) * 0.04))
    r.add("bracer", E(0.045, 0.045, 0.06), "leather_dk", "elbow_l", loc=(0, 0, -0.12))
    r.add("belt", E(0.14, 0.11, 0.025), "leather_dk", "pelvis", loc=(0, 0, 0.04))
    # The quiver, slung across the back, arrows up over the right shoulder.
    r.limb("quiver", (0.10, 0.12, 0.08), (-0.06, 0.12, 0.40), 0.05, "leather_dk", "chest", r_tip=0.055)
    for k in range(3):
        r.limb("arrow", (-0.04 - k * 0.025, 0.12, 0.40), (-0.08 - k * 0.03, 0.14, 0.52), 0.012, "fletch", "chest",
               r_tip=0.01)
    # The longbow: two limbs curving forward from the grip, and the string.
    for sgn in (-1, 1):
        r.limb("bowlimb", (0, 0, 0), (0, -0.07, sgn * 0.24), 0.02, "archer_bow", "hand_l", r_tip=0.018)
        r.limb("bowtip", (0, -0.07, sgn * 0.24), (0, -0.03, sgn * 0.44), 0.018, "archer_bow", "hand_l", r_tip=0.01)
    r.limb("string", (0, 0.0, -0.44), (0, 0.0, 0.44), 0.006, "bowstring", "hand_l", r_tip=0.006)
    r.add("grip", E(0.03, 0.03, 0.05), "leather_dk", "hand_l")
    return r.fit("bone_archer", 30, ARCHER_REST)


ARCHER_REST = {"shoulder_l": fwd(10), "elbow_l": X(-12), "hand_l": (0, 0, 70),
               "shoulder_r": fwd(8), "elbow_r": X(-20)}


def archer_idle(t):
    s = sn(t)
    v = dict(ARCHER_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 6 * sn(t, 0.25))})
    return v


def archer_walk(t):
    v = gait(t, 28, 24, 0, 0.025, 4)
    v.update(ARCHER_REST)
    v.update({"shoulder_r": fwd(8 + 18 * sn(t)), "shoulder_l": fwd(10 - 10 * sn(t))})
    return v


def archer_attack(t):
    # Bow up, arrow drawn to the cheek, and loosed.
    aim = {"shoulder_l": fwd(88), "elbow_l": X(0), "hand_l": (90, 0, 0), "chest": (0, 0, -20),
           "shoulder_r": fwd(84), "elbow_r": X(-120), "hand_r": X(0), "head": (0, 0, 10)}
    loose = dict(aim)
    loose.update({"shoulder_r": fwd(60), "elbow_r": X(-60), "chest": (4, 0, -16)})
    i, k = phases(t, 0.30, 0.70, 0.80, 1.0)
    return [mix(ARCHER_REST, aim, k), aim, mix(aim, loose, k), mix(loose, ARCHER_REST, k), ARCHER_REST][i]


def archer_hurt(t):
    v = dict(ARCHER_REST)
    v.update(struck(math.sin(t * math.pi)))
    return v


def archer_death(t):
    return cr.skel_death(t)


# --- Cryptbound --------------------------------------------------------------------------
def build_cryptbound():
    """Somebody buried the way you bury something you are afraid of: wound
    head to foot in grave linen, chained over the top of it, and shackled at
    both wrists -- the chains broken. One eye shows between the wrappings, and
    it is not an eye."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.60))
    r.add("hips", E(0.15, 0.12, 0.11), "linen", "pelvis")
    humanoid_legs(r, -0.03, 0.09, 0.27, 0.27, 0.06, "linen", "linen_dk", foot_col="linen_dk")
    for side in ("l", "r"):
        for k in range(3):
            r.add("wrap", TORUS(0.058, 0.012), "linen_dk", "hip_" + side if k < 2 else "knee_" + side,
                  loc=(0, 0, -0.08 - k * 0.10 if k < 2 else -0.12), rot=(0.25 * (1 - 2 * (k % 2)), 0, 0))
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(8, 0, 0))
    r.add("torso", E(0.18, 0.14, 0.25), "linen", "chest", loc=(0, 0, 0.22))
    for k in range(4):
        r.add("wrap", TORUS(0.17 - abs(k - 1.5) * 0.02, 0.014), "linen_dk", "chest", loc=(0, 0, 0.08 + k * 0.10),
              rot=(0.18 * (1 - 2 * (k % 2)), 0.1, 0))
    r.add("chain", TORUS(0.20, 0.022), "iron_dk", "chest", loc=(0, 0, 0.26), rot=(0.1, 0.55, 0))
    r.add("chain2", TORUS(0.20, 0.022), "iron_dk", "chest", loc=(0, 0, 0.24), rot=(0.1, -0.55, 0))
    humanoid_arms(r, 0.40, 0.20, 0.24, 0.23, 0.055, "linen", "linen_dk", flare=12)
    for side in ("l", "r"):
        r.add("shackle", TORUS(0.055, 0.02), "iron_dk", "elbow_" + side, loc=(0, 0, -0.18))
        r.limb("link", (0, 0.05, -0.18), (0, 0.10, -0.30), 0.02, "iron_dk", "elbow_" + side, r_tip=0.018)
        r.limb("strip", (0.02, 0.03, -0.10), (0.05, 0.14, -0.30), 0.018, "linen", "elbow_" + side, r_tip=0.01)
        fingers(r, side, "linen_dk", r_=0.014, reach=0.07, spread=0.035)
    for x in (-0.10, 0.08):
        r.limb("strip", (x, 0.08, -0.02), (x * 1.4, 0.20, -0.36), 0.02, "linen", "pelvis", r_tip=0.01)
    r.joint("neck", (0, -0.02, 0.48), "chest", rest=(6, 0, 0))
    r.joint("head", (0, -0.01, 0.07), "neck", rest=(-10, 0, 0))
    r.add("skull", E(0.10, 0.105, 0.115), "linen", "head", loc=(0, 0, 0.07))
    for k in range(3):
        r.add("wrap", TORUS(0.10, 0.012), "linen_dk", "head", loc=(0, 0, 0.02 + k * 0.05),
              rot=(0.3 * (1 - 2 * (k % 2)), 0, 0))
    r.add("gap", E(0.045, 0.02, 0.018), "shroud_dk", "head", loc=(-0.035, -0.095, 0.085))
    r.add("eye", E(0.02, 0.012, 0.014), "wisp_glow", "head", loc=(-0.035, -0.105, 0.085))
    return r.fit("cryptbound", 32, BOUND_REST)


BOUND_REST = {"shoulder_l": fwd(60), "elbow_l": X(-14), "shoulder_r": fwd(52), "elbow_r": X(-18)}


def bound_idle(t):
    s = sn(t)
    v = dict(BOUND_REST)
    v.update({"_z": 0.006 * s, "chest": X(8 + 2 * s), "head": (0, 0, 5 * sn(t, 0.3)),
              "shoulder_l": fwd(60 + 4 * s), "shoulder_r": fwd(52 - 4 * s)})
    return v


def bound_walk(t):
    # Stiff: the knees barely bend and the arms stay out in front.
    s = sn(t)
    v = dict(BOUND_REST)
    v.update({"hip_l": fwd(22 * s), "hip_r": fwd(-22 * s), "knee_l": X(8 * max(0.0, -s)),
              "knee_r": X(8 * max(0.0, s)), "_z": 0.015 * abs(s), "chest": (8, 0, 6 * s),
              "shoulder_l": fwd(62 + 6 * s), "shoulder_r": fwd(54 - 6 * s), "pelvis": (0, 5 * s, 0)})
    return v


def bound_attack(t):
    lift = {"shoulder_l": fwd(-150), "shoulder_r": fwd(-150), "elbow_l": X(-20), "elbow_r": X(-20),
            "chest": X(-10), "_y": 0.03}
    crush = {"shoulder_l": fwd(70), "shoulder_r": fwd(70), "elbow_l": X(-4), "elbow_r": X(-4), "chest": X(26),
             "_y": -0.10, "hip_l": fwd(20)}
    return swing(t, BOUND_REST, lift, crush)


def bound_hurt(t):
    v = dict(BOUND_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=fwd(-40), shoulder_r=fwd(-40)))
    return v


def bound_death(t):
    return topple(t)


# --- Bone Knight ------------------------------------------------------------------------
def build_bone_knight():
    """A knight buried in his armour and not done with it: a great helm with a
    red crest, breastplate, pauldrons and greaves over bone, a tabard gone to
    rags, a kite shield and a longsword that has been kept sharp."""
    r = Rig()
    _bones(r, eye="wisp_glow")
    r.add("breast", E(0.19, 0.15, 0.19), "knight_steel", "chest", loc=(0, -0.01, 0.24))
    r.add("fauld", C(0.15, 0.17, 0.10, squash_y=0.85), "knight_steel_dk", "pelvis", loc=(0, 0, 0.06))
    r.add("tabard", E(0.10, 0.03, 0.20), "knight_tabard", "chest", loc=(0, -0.15, 0.12))
    r.add("tabard2", E(0.08, 0.03, 0.14), "knight_tabard", "pelvis", loc=(0, -0.12, -0.12))
    r.add("tabard_mark", E(0.04, 0.02, 0.04), "grave_gold", "chest", loc=(0, -0.175, 0.20))
    for side in ("l", "r"):
        r.add("pauldron", E(0.10, 0.10, 0.07), "knight_steel", "shoulder_" + side, loc=(0, 0, 0.02))
        r.add("greave", E(0.055, 0.055, 0.12), "knight_steel_dk", "knee_" + side, loc=(0, -0.01, -0.12))
        r.add("gauntlet", E(0.045, 0.045, 0.05), "knight_steel_dk", "hand_" + side, loc=(0, 0, 0.02))
    r.add("helm", E(0.12, 0.125, 0.14), "knight_steel", "head", loc=(0, 0, 0.08))
    r.add("visor", E(0.085, 0.03, 0.02), "shroud_dk", "head", loc=(0, -0.115, 0.09))
    for sx in (-1, 1):
        r.add("glint", E(0.018, 0.01, 0.012), "wisp_glow", "head", loc=(sx * 0.035, -0.13, 0.09))
    for k in range(4):
        r.limb("crest", (0, -0.06 + k * 0.05, 0.20), (0, -0.02 + k * 0.07, 0.30 - k * 0.02), 0.03, "knight_tabard",
               "head", r_tip=0.012)
    # The longsword and the kite shield.
    r.limb("blade", (0, 0, 0.02), (0, 0, 0.60), 0.034, "knight_blade", "hand_r", r_tip=0.01)
    r.add("guard", E(0.10, 0.03, 0.022), "knight_steel_dk", "hand_r", loc=(0, 0, 0.0))
    r.add("pommel", E(0.028, 0.028, 0.028), "grave_gold", "hand_r", loc=(0, 0, -0.08))
    r.add("shield", E(0.15, 0.035, 0.19), "knight_steel", "hand_l", loc=(0, -0.05, -0.04))
    r.add("shield_face", E(0.12, 0.02, 0.15), "knight_tabard", "hand_l", loc=(0, -0.075, -0.04))
    r.add("shield_mark", E(0.035, 0.012, 0.05), "grave_gold", "hand_l", loc=(0, -0.09, -0.02))
    return r.fit("bone_knight", 36, KNIGHT_REST)


KNIGHT_REST = {"shoulder_r": fwd(14), "elbow_r": X(-58), "hand_r": X(20), "shoulder_l": fwd(24), "elbow_l": X(-66)}


def knight_idle(t):
    s = sn(t)
    v = dict(KNIGHT_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 4 * sn(t, 0.25))})
    return v


def knight_walk(t):
    v = gait(t, 30, 26, 0, 0.025, 4)
    v.update(KNIGHT_REST)
    v.update({"shoulder_r": fwd(16 + 6 * sn(t))})
    return v


def knight_attack(t):
    raise_ = {"shoulder_r": fwd(-74), "elbow_r": X(-96), "hand_r": X(30), "chest": (-6, 0, -14), "_y": 0.03,
              "shoulder_l": fwd(40), "elbow_l": X(-80)}
    chop = {"shoulder_r": fwd(64), "elbow_r": X(-12), "hand_r": X(10), "chest": (18, 0, 16), "_y": -0.10,
            "hip_l": fwd(24), "hip_r": fwd(-16), "shoulder_l": fwd(30), "elbow_l": X(-70)}
    return swing(t, KNIGHT_REST, raise_, chop, (0.36, 0.56, 1.0))


def knight_hurt(t):
    v = dict(KNIGHT_REST)
    v.update(struck(math.sin(t * math.pi)))
    return v


def knight_death(t):
    return cr.skel_death(t)


# --- Plague Corpse ----------------------------------------------------------------------
def build_plague_corpse():
    """Dead of the sickness and swollen with it: a belly like a barrel under a
    stained shroud, bruised and boiled all over, and the air about it green."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.50))
    r.add("hips", E(0.20, 0.16, 0.13), "plague", "pelvis")
    humanoid_legs(r, -0.03, 0.12, 0.23, 0.23, 0.07, "plague", "plague_dk", foot_col="plague_dk")
    r.joint("chest", (0, 0, 0.06), "pelvis", rest=(10, 0, 5))
    r.add("belly", E(0.28, 0.26, 0.27), "plague", "chest", loc=(0, -0.03, 0.18))
    r.add("chestp", E(0.22, 0.17, 0.16), "plague", "chest", loc=(0, 0, 0.38))
    for x, y, z, rr in ((-0.15, -0.20, 0.26, 0.05), (0.18, -0.16, 0.12, 0.04), (0.02, -0.26, 0.08, 0.035),
                        (-0.22, -0.02, 0.10, 0.04)):
        r.add("bruise", E(rr * 1.6, rr, rr * 1.4), "plague_dk", "chest", loc=(x, y, z))
    for x, y, z in ((0.10, -0.24, 0.24), (-0.06, -0.26, 0.16), (0.20, -0.10, 0.32), (-0.18, -0.12, 0.38),
                    (0.06, -0.20, 0.40)):
        r.add("boil", E(0.03, 0.03, 0.03), "plague_boil_glow", "chest", loc=(x, y, z))
    # The shroud over head and shoulders.
    r.add("shroud", E(0.25, 0.20, 0.12), "shroud_stain", "chest", loc=(0, 0.02, 0.48))
    for k in range(4):
        r.add("hem", E(0.06, 0.04, 0.08), "shroud_stain_dk", "chest", loc=(-0.18 + k * 0.12, 0.10, 0.34))
    humanoid_arms(r, 0.44, 0.25, 0.22, 0.22, 0.055, "plague", "plague_dk", flare=18)
    for side in ("l", "r"):
        fingers(r, side, "plague_dk", r_=0.015, reach=0.08, spread=0.04)
    r.joint("neck", (0, -0.04, 0.50), "chest", rest=(14, 0, 0))
    r.joint("head", (0, -0.01, 0.06), "neck", rest=(-12, 0, 0))
    r.add("skull", E(0.11, 0.115, 0.115), "plague", "head", loc=(0, 0, 0.07))
    r.add("hood", E(0.13, 0.13, 0.12), "shroud_stain", "head", loc=(0, 0.03, 0.11))
    r.add("mouth", E(0.045, 0.03, 0.045), "ghoul_mouth", "head", loc=(0, -0.10, 0.01))
    r.limb("drool", (0, -0.11, -0.01), (0, -0.12, -0.10), 0.014, "plague_ooze", "head", r_tip=0.006)
    for sx in (-1, 1):
        r.add("eye", E(0.022, 0.014, 0.02), "plague_boil_glow", "head", loc=(sx * 0.045, -0.105, 0.085))
    # The miasma: three puffs of it hanging round, turning slowly.
    r.joint("miasma", (0, 0, 0.62), "pelvis")
    for k in range(3):
        a = k * math.tau / 3 + 0.5
        r.add("puff", E(0.07, 0.07, 0.055), "plague_gas", "miasma",
              loc=(0.36 * math.cos(a), 0.30 * math.sin(a), 0.08 * math.sin(3 * a)))
    return r.fit("plague_corpse", 36)


def plague_idle(t):
    v = cr.zom_idle(t)
    v["miasma"] = (0, 0, 120 * t)
    return v


def plague_walk(t):
    v = cr.zom_walk(t)
    v["miasma"] = (0, 0, 120 * t)
    return v


def plague_attack(t):
    # It retches over you: bent double, arms out, and the miasma with it.
    lean = {"chest": X(-10), "neck": X(-14), "shoulder_l": fwd(40), "shoulder_r": fwd(40), "_y": 0.02}
    retch = {"chest": X(40), "neck": X(24), "head": X(14), "shoulder_l": fwd(80), "shoulder_r": fwd(80),
             "elbow_l": X(-10), "elbow_r": X(-10), "_y": -0.10, "miasma": (0, 0, 90)}
    return swing(t, {}, lean, retch, (0.40, 0.60, 1.0))


def plague_hurt(t):
    v = cr.zom_hurt(t)
    v["_s"] = 0.05 * math.sin(t * math.pi)
    return v


def plague_death(t):
    v = cr.zom_death(t)
    v["miasma"] = (0, 0, 90 * t)
    return v


# --- Tomb Shade --------------------------------------------------------------------------
def build_tomb_shade():
    """A shadow that stood up off a tomb wall and kept the shape of whoever
    cast it: no feet, long arms, and in place of a face a pale mask with two
    holes and the light behind them."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.90))
    r.add("trail", C(0.17, 0.03, 0.60, squash_y=0.85), "shade", "pelvis", loc=(0, 0.04, -0.16))
    for k in range(5):
        r.limb("tatter", (-0.14 + k * 0.07, 0.0, -0.20), (-0.18 + k * 0.09, 0.08, -0.66 - (k % 2) * 0.08), 0.04,
               "shade" if k % 2 else "shade_lt", "pelvis", r_tip=0.008)
    r.joint("chest", (0, 0, 0.12), "pelvis", rest=(-2, 0, 0))
    r.add("torso", E(0.15, 0.11, 0.24), "shade", "chest", loc=(0, 0, 0.16))
    r.add("shoulders", E(0.20, 0.12, 0.07), "shade_lt", "chest", loc=(0, 0.01, 0.32))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.18, 0, 0.30), "chest", rest=(0, sx * -26, 0))
        r.limb("arm", (0, 0, 0), (0, 0, -0.30), 0.04, "shade", "shoulder_" + side, r_tip=0.03)
        r.joint("elbow_" + side, (0, 0, -0.30), "shoulder_" + side, rest=(-16, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.28), 0.03, "shade_lt", "elbow_" + side, r_tip=0.02)
        r.joint("hand_" + side, (0, 0, -0.28), "elbow_" + side)
        fingers(r, side, "shade_glow", n=3, r_=0.012, reach=0.12, spread=0.035, down=0.08)
    r.joint("neck", (0, 0, 0.42), "chest")
    r.joint("head", (0, -0.01, 0.06), "neck", rest=(-6, 0, 0))
    r.add("hood", E(0.13, 0.14, 0.15), "shade", "head", loc=(0, 0.02, 0.08))
    r.add("mask", E(0.11, 0.05, 0.14), "shade_mask", "head", loc=(0, -0.12, 0.06))
    for sx in (-1, 1):
        r.add("hole", E(0.03, 0.016, 0.036), "shade_glow", "head", loc=(sx * 0.045, -0.166, 0.08))
        for k in range(2):
            r.limb("tendril", (sx * 0.06, 0.04, 0.14 - k * 0.04), (sx * (0.12 + k * 0.05), 0.22 + k * 0.08, 0.26 - k * 0.12),
                   0.03, "shade", "head", r_tip=0.006)
    r.add("mouthslit", E(0.035, 0.012, 0.012), "shade", "head", loc=(0, -0.168, 0.0))
    return r.fit("tomb_shade", 40)


# --- Grave Hound ------------------------------------------------------------------------
def build_grave_hound():
    """A hound that was buried with its master and dug itself out: mangy black
    hide over half of it, the ribs showing through the rest, a skull for a
    head and a spiked collar with the chain snapped off it."""
    r = Rig()
    r.joint("body", (0, 0, 0.44), rest=(-4, 0, 0))
    r.add("chest", E(0.13, 0.16, 0.13), "ghound_fur", "body", loc=(0, -0.15, 0.03))
    r.add("hips", E(0.12, 0.12, 0.12), "ghound_fur", "body", loc=(0, 0.22, 0.02))
    for k in range(4):
        r.add("rib", TORUS(0.105 - abs(k - 1.5) * 0.01, 0.012), "bone_w", "body", loc=(0, -0.04 + k * 0.055, 0.0),
              rot=(1.57, 0, 0))
    r.limb("spine", (0, -0.20, 0.12), (0, 0.24, 0.10), 0.025, "bone_g", "body", r_tip=0.02)
    for k in range(4):
        r.limb("spur", (0, -0.14 + k * 0.12, 0.12), (0, -0.16 + k * 0.12, 0.19), 0.02, "bone_w", "body", r_tip=0.004)
    for sx, side in ((-1, "l"), (1, "r")):
        for tag, y in (("fore", -0.16), ("hind", 0.20)):
            j = tag + "_" + side
            r.joint(j, (sx * 0.10, y, -0.06), "body", rest=(8 if tag == "hind" else 4, 0, 0))
            r.limb("thigh", (0, 0, 0), (0, 0.02 if tag == "hind" else -0.02, -0.18), 0.045,
                   "ghound_fur" if tag == "hind" else "bone_w", j, r_tip=0.03)
            r.joint(j + "_knee", (0, 0.02 if tag == "hind" else -0.02, -0.18), j, rest=(-18, 0, 0))
            r.limb("shin", (0, 0, 0), (0, 0, -0.18), 0.026, "bone_g", j + "_knee", r_tip=0.02)
            r.add("paw", E(0.04, 0.06, 0.03), "ghound_fur", j + "_knee", loc=(0, -0.02, -0.19))
    r.joint("neck", (0, -0.26, 0.06), "body", rest=(14, 0, 0))
    r.limb("neckp", (0, 0, 0), (0, -0.10, 0.04), 0.055, "ghound_fur", "neck", r_tip=0.05)
    r.add("collar", TORUS(0.06, 0.018), "iron_dk", "neck", loc=(0, -0.04, 0.02), rot=(1.3, 0, 0))
    for k in range(4):
        a = k * math.tau / 4
        r.limb("stud", (math.cos(a) * 0.06, -0.04, 0.02 + math.sin(a) * 0.06),
               (math.cos(a) * 0.10, -0.04, 0.02 + math.sin(a) * 0.10), 0.014, "iron_rust_lt", "neck", r_tip=0.003)
    r.limb("chain", (0, -0.02, -0.04), (0.02, 0.02, -0.16), 0.016, "iron_dk", "neck", r_tip=0.014)
    r.joint("head", (0, -0.10, 0.04), "neck", rest=(-10, 0, 0))
    r.add("skull", E(0.075, 0.09, 0.07), "bone_w", "head")
    r.add("muzzle", E(0.045, 0.11, 0.04), "bone_w", "head", loc=(0, -0.13, -0.02))
    r.add("jaw", E(0.04, 0.09, 0.022), "bone_g", "head", loc=(0, -0.12, -0.06))
    for sx in (-1, 1):
        r.add("socket", E(0.026, 0.02, 0.024), "bone_dk", "head", loc=(sx * 0.042, -0.07, 0.03))
        r.add("eye", E(0.018, 0.012, 0.016), "ghound_eye_glow", "head", loc=(sx * 0.042, -0.082, 0.03))
        r.add("ear", E(0.02, 0.03, 0.05), "ghound_fur", "head", loc=(sx * 0.05, 0.03, 0.07), rot=(0, sx * -0.3, 0))
        for k in range(3):
            r.limb("tooth", (sx * 0.026, -0.14 - k * 0.03, -0.04), (sx * 0.026, -0.14 - k * 0.03, -0.065),
                   0.009, "tooth", "head", r_tip=0.002)
    r.joint("tail1", (0, 0.32, 0.04), "body", rest=(52, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.20), 0.02, "bone_g", "tail1", r_tip=0.015)
    r.joint("tail2", (0, 0, -0.20), "tail1", rest=(14, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.16), 0.015, "bone_w", "tail2", r_tip=0.006)
    return r.fit("grave_hound", 30)


# --- Blood Thrall -----------------------------------------------------------------------
def build_blood_thrall():
    """One of the house's servants, kept on after death to keep the house: in
    what is left of its livery -- a red waistcoat, a shirt that was white --
    bent over with its hands up, and blood down its chin."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.60))
    r.add("hips", E(0.14, 0.11, 0.10), "thrall_black", "pelvis")
    humanoid_legs(r, -0.03, 0.09, 0.27, 0.27, 0.055, "thrall_black", "thrall_black", foot_col="thrall_skin_dk")
    r.joint("chest", (0, 0, 0.07), "pelvis", rest=(22, 0, 0))
    r.add("shirt", E(0.16, 0.12, 0.23), "thrall_shirt", "chest", loc=(0, 0, 0.20))
    r.add("waistcoat", E(0.165, 0.125, 0.15), "thrall_coat", "chest", loc=(0, 0.005, 0.15))
    for k in range(3):
        r.add("button", E(0.014, 0.01, 0.014), "grave_gold", "chest", loc=(0, -0.13, 0.08 + k * 0.06))
    r.add("tear", E(0.05, 0.02, 0.07), "thrall_skin_dk", "chest", loc=(0.06, -0.12, 0.30))
    r.add("stain", E(0.06, 0.02, 0.05), "blood", "chest", loc=(-0.03, -0.12, 0.33))
    humanoid_arms(r, 0.38, 0.19, 0.23, 0.22, 0.045, "thrall_shirt", "thrall_skin", flare=12)
    for side in ("l", "r"):
        fingers(r, side, "thrall_nail", r_=0.012, reach=0.10, spread=0.035, down=0.06)
    r.joint("neck", (0, -0.03, 0.44), "chest", rest=(-14, 0, 0))
    r.joint("head", (0, -0.01, 0.07), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.095, 0.10, 0.11), "thrall_skin", "head", loc=(0, 0, 0.07))
    r.add("hair", E(0.10, 0.10, 0.07), "thrall_hair", "head", loc=(0, 0.02, 0.12))
    for x in (-0.07, 0.0, 0.07):
        r.limb("tuft", (x, 0.02, 0.15), (x * 1.8, 0.10, 0.22), 0.025, "thrall_hair", "head", r_tip=0.006)
    r.add("mouth", E(0.035, 0.02, 0.02), "ghoul_mouth", "head", loc=(0, -0.095, 0.01))
    r.limb("drip", (0.01, -0.10, 0.0), (0.012, -0.10, -0.07), 0.012, "blood", "head", r_tip=0.006)
    for sx in (-1, 1):
        r.add("eye", E(0.02, 0.012, 0.016), "thrall_eye_glow", "head", loc=(sx * 0.038, -0.095, 0.085))
    return r.fit("blood_thrall", 30, THRALL_REST)


THRALL_REST = {"shoulder_l": fwd(44), "elbow_l": X(-60), "shoulder_r": fwd(40), "elbow_r": X(-64)}


def thrall_idle(t):
    s = sn(t)
    v = dict(THRALL_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 10 * sn(t, 0.3)),
              "hand_l": X(10 * s), "hand_r": X(-10 * s)})
    return v


def thrall_walk(t):
    v = gait(t, 34, 32, 0, 0.035, 8)
    v.update(THRALL_REST)
    v.update({"shoulder_l": fwd(44 - 10 * sn(t)), "shoulder_r": fwd(40 + 10 * sn(t)), "head": (0, 0, -6 * sn(t))})
    return v


def thrall_attack(t):
    # Right hand, then left: a flurry at the throat.
    i, k = phases(t, 0.30, 0.55, 0.80, 1.0)
    a = {"shoulder_r": fwd(-40), "elbow_r": X(-90), "chest": (14, 0, -14), "shoulder_l": fwd(30)}
    b = {"shoulder_r": fwd(80), "elbow_r": X(-6), "chest": (26, 0, 14), "_y": -0.08,
         "shoulder_l": fwd(-30), "elbow_l": X(-90)}
    c = {"shoulder_l": fwd(84), "elbow_l": X(-6), "chest": (28, 0, -14), "_y": -0.12, "shoulder_r": fwd(40)}
    return [mix(THRALL_REST, a, k), mix(a, b, k), mix(b, c, k), mix(c, THRALL_REST, k), THRALL_REST][i]


def thrall_hurt(t):
    v = dict(THRALL_REST)
    v.update(struck(math.sin(t * math.pi)))
    return v


def thrall_death(t):
    return topple(t)


# --- Bone Colossus ----------------------------------------------------------------------
def build_bone_colossus():
    """Every bone the lower vault had spare, put together into one thing twice
    the height of a man: legs of bundled femurs, a ribcage with the skulls of
    the rest packed into it, a horned skull for a head and a club made of the
    thighbone of something that was never a man at all."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.64))
    r.add("hipbone", E(0.30, 0.18, 0.14), "bone_g", "pelvis")
    r.add("sacrum", E(0.12, 0.10, 0.12), "bone_w", "pelvis", loc=(0, 0.06, 0.06))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.20, 0, -0.06), "pelvis")
        for k in (-1, 1):
            r.limb("femur", (k * 0.035, 0, 0), (k * 0.03, 0, -0.30), 0.055, "bone_w" if k < 0 else "bone_g",
                   "hip_" + side, r_tip=0.05)
        r.add("kneecap", E(0.07, 0.07, 0.07), "bone_w", "hip_" + side, loc=(0, -0.04, -0.30))
        r.joint("knee_" + side, (0, 0, -0.30), "hip_" + side)
        for k in (-1, 1):
            r.limb("shin", (k * 0.03, 0, 0), (k * 0.04, 0, -0.28), 0.05, "bone_g" if k < 0 else "bone_w",
                   "knee_" + side, r_tip=0.055)
        r.add("foot", E(0.11, 0.16, 0.05), "bone_g", "knee_" + side, loc=(0, -0.06, -0.30))
        for k in (-1, 0, 1):
            r.limb("toe", (k * 0.06, -0.14, -0.31), (k * 0.08, -0.24, -0.33), 0.028, "bone_w", "knee_" + side,
                   r_tip=0.01)
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(12, 0, 0))
    r.limb("spine", (0, 0.10, -0.04), (0, 0.10, 0.60), 0.06, "bone_g", "chest", r_tip=0.05)
    # The cage, and what is in it.
    r.add("cavity", E(0.30, 0.20, 0.28), "bone_cavity", "chest", loc=(0, 0.0, 0.30))
    for k in range(5):
        wide = 0.34 - abs(k - 2) * 0.04
        r.add("rib", TORUS(wide, 0.028), "bone_w", "chest", loc=(0, 0.0, 0.10 + k * 0.10), rot=(0.25, 0, 0))
    r.add("sternum", E(0.05, 0.05, 0.22), "bone_w", "chest", loc=(0, -0.28, 0.30))
    for x, y, z in ((-0.12, -0.08, 0.22), (0.10, -0.10, 0.32), (0.0, -0.04, 0.46), (-0.08, -0.02, 0.38),
                    (0.14, -0.04, 0.16)):
        r.add("skull", E(0.07, 0.07, 0.075), "bone_w", "chest", loc=(x, y, z))
        for sx in (-1, 1):
            r.add("socket", E(0.02, 0.012, 0.02), "bone_dk", "chest", loc=(x + sx * 0.028, y - 0.064, z + 0.01))
    r.add("glow", E(0.06, 0.05, 0.06), "colossus_glow", "chest", loc=(0, -0.02, 0.30))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.44, 0.02, 0.58), "chest", rest=(0, sx * -18, 0))
        r.add("scapula", E(0.16, 0.10, 0.12), "bone_g", "shoulder_" + side, loc=(0, 0.04, 0.03))
        for k in (-1, 1):
            r.limb("humerus", (k * 0.03, 0, 0), (k * 0.03, 0, -0.36), 0.05, "bone_w", "shoulder_" + side,
                   r_tip=0.045)
        r.joint("elbow_" + side, (0, 0, -0.36), "shoulder_" + side, rest=(-14, 0, 0))
        for k in (-1, 1):
            r.limb("radius", (k * 0.03, 0, 0), (k * 0.03, 0, -0.34), 0.045, "bone_g", "elbow_" + side, r_tip=0.04)
        r.joint("hand_" + side, (0, 0, -0.34), "elbow_" + side)
        r.add("hand", E(0.09, 0.08, 0.08), "bone_g", "hand_" + side)
        fingers(r, side, "bone_w", n=4, r_=0.022, reach=0.12, spread=0.05, down=0.08)
    # The club: a huge thighbone, skulls lashed round the knuckle end.
    r.limb("club", (0, 0, 0.06), (0, 0, -0.40), 0.07, "bone_w", "hand_r", r_tip=0.10)
    r.add("knuckle", E(0.14, 0.12, 0.12), "bone_w", "hand_r", loc=(0, 0, -0.44))
    for k in range(3):
        a = k * math.tau / 3
        r.add("clubskull", E(0.06, 0.06, 0.065), "bone_g", "hand_r",
              loc=(math.cos(a) * 0.13, math.sin(a) * 0.13, -0.34))
    r.add("lash", TORUS(0.10, 0.02), "leather_dk", "hand_r", loc=(0, 0, -0.28))
    r.joint("neck", (0, -0.04, 0.66), "chest")
    r.joint("head", (0, -0.04, 0.08), "neck", rest=(-12, 0, 0))
    r.add("skull", E(0.17, 0.19, 0.15), "bone_w", "head", loc=(0, -0.02, 0.10))
    r.add("snout", E(0.10, 0.14, 0.09), "bone_w", "head", loc=(0, -0.18, 0.04))
    r.add("jaw", E(0.12, 0.13, 0.05), "bone_g", "head", loc=(0, -0.12, -0.06))
    for sx in (-1, 1):
        r.add("socket", E(0.05, 0.03, 0.045), "bone_dk", "head", loc=(sx * 0.08, -0.15, 0.13))
        r.add("eye", E(0.028, 0.018, 0.026), "colossus_glow", "head", loc=(sx * 0.08, -0.17, 0.13))
        r.limb("horn", (sx * 0.14, 0.02, 0.18), (sx * 0.36, 0.04, 0.24), 0.06, "bone_g", "head", r_tip=0.04)
        r.limb("horn2", (sx * 0.36, 0.04, 0.24), (sx * 0.38, -0.08, 0.44), 0.04, "bone_w", "head", r_tip=0.006)
        for k in range(3):
            r.limb("tooth", (sx * (0.03 + k * 0.03), -0.28, -0.02), (sx * (0.03 + k * 0.03), -0.29, -0.08), 0.014,
                   "tooth", "head", r_tip=0.004)
    return r.fit("bone_colossus", 58)


def colossus_death(t):
    # It comes apart where it stands, the way the small ones do, only further.
    v = cr.skel_death(t)
    v["_z"] = -0.30 * ease(t)
    return v


# --- Nosferatu -------------------------------------------------------------------------
def build_nosferatu():
    """The old kind, not the lord's kind: bald as an egg and the colour of one
    gone bad, ears to a point, two front teeth like a rat's, in a black coat
    buttoned to the chin, and hands held up in front of it all fingers."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.66))
    r.add("hips", E(0.13, 0.10, 0.10), "nosf_coat", "pelvis")
    humanoid_legs(r, -0.03, 0.08, 0.30, 0.30, 0.045, "nosf_coat", "nosf_coat_dk", foot_col="nosf_coat_dk")
    r.add("skirt", C(0.15, 0.20, 0.30, squash_y=0.8), "nosf_coat", "pelvis", loc=(0, 0.02, 0.0))
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(18, 0, 0))
    r.add("coat", E(0.16, 0.12, 0.26), "nosf_coat", "chest", loc=(0, 0, 0.22))
    for k in range(5):
        r.add("button", E(0.014, 0.01, 0.014), "grave_gold", "chest", loc=(0, -0.12, 0.08 + k * 0.07))
    r.add("collar", E(0.14, 0.10, 0.08), "nosf_coat_dk", "chest", loc=(0, 0.02, 0.46))
    humanoid_arms(r, 0.40, 0.19, 0.25, 0.25, 0.045, "nosf_coat", "nosf_coat", flare=10)
    for side in ("l", "r"):
        r.add("hand", E(0.04, 0.035, 0.05), "nosf_skin", "hand_" + side)
        fingers(r, side, "nosf_skin", n=4, r_=0.011, reach=0.14, spread=0.03, down=0.10)
    r.joint("neck", (0, -0.03, 0.48), "chest", rest=(-10, 0, 0))
    r.joint("head", (0, -0.02, 0.07), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.10, 0.11, 0.14), "nosf_skin", "head", loc=(0, 0.01, 0.10))
    r.add("dome", E(0.09, 0.10, 0.08), "nosf_skin", "head", loc=(0, 0.03, 0.20))
    r.add("brow", E(0.085, 0.04, 0.03), "nosf_skin_dk", "head", loc=(0, -0.08, 0.13))
    r.limb("nose", (0, -0.09, 0.12), (0, -0.14, 0.05), 0.022, "nosf_skin_dk", "head", r_tip=0.01)
    for sx in (-1, 1):
        r.add("socket", E(0.028, 0.018, 0.022), "nosf_skin_dk", "head", loc=(sx * 0.04, -0.09, 0.10))
        r.add("eye", E(0.016, 0.01, 0.014), "thrall_eye_glow", "head", loc=(sx * 0.04, -0.10, 0.10))
        r.limb("ear", (sx * 0.09, 0.0, 0.10), (sx * 0.19, 0.06, 0.20), 0.035, "nosf_skin", "head", r_tip=0.005)
        r.limb("fang", (sx * 0.012, -0.10, 0.02), (sx * 0.012, -0.105, -0.04), 0.012, "tooth", "head", r_tip=0.004)
    return r.fit("nosferatu", 42, NOSF_REST)


NOSF_REST = {"shoulder_l": fwd(40), "elbow_l": X(-88), "hand_l": X(-30),
             "shoulder_r": fwd(36), "elbow_r": X(-92), "hand_r": X(-30)}


def nosf_idle(t):
    s = sn(t)
    v = dict(NOSF_REST)
    # The fingers work, as if at something they have not got hold of yet.
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 6 * sn(t, 0.3)),
              "hand_l": X(-30 + 14 * s), "hand_r": X(-30 - 14 * s)})
    return v


def nosf_walk(t):
    # It glides: short stiff steps, the body not rising at all.
    s = sn(t)
    v = dict(NOSF_REST)
    v.update({"hip_l": fwd(18 * s), "hip_r": fwd(-18 * s), "knee_l": X(10 * max(0.0, -s)),
              "knee_r": X(10 * max(0.0, s)), "chest": (20, 0, 3 * s), "head": (0, 0, 4 * sn(t, 0.2))})
    return v


def nosf_attack(t):
    rear = {"shoulder_l": (-140, 20, 0), "shoulder_r": (-140, -20, 0), "elbow_l": X(-40), "elbow_r": X(-40),
            "chest": X(-6), "head": X(-14), "_z": 0.03, "_y": 0.02}
    rake = {"shoulder_l": fwd(70), "shoulder_r": fwd(70), "elbow_l": X(-8), "elbow_r": X(-8), "chest": X(30),
            "head": X(14), "_y": -0.12}
    return swing(t, NOSF_REST, rear, rake, (0.42, 0.60, 1.0))


def nosf_hurt(t):
    v = dict(NOSF_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=fwd(-20), shoulder_r=fwd(-20)))
    return v


def nosf_death(t):
    # Down onto its knees and forward onto its face, folding as it goes.
    k = ease(t)
    return {"_z": -0.30 * k, "hip_l": fwd(70 * k), "hip_r": fwd(70 * k), "knee_l": X(110 * k), "knee_r": X(110 * k),
            "chest": X(18 + 50 * k), "neck": X(20 * k), "shoulder_l": fwd(40 - 30 * k), "shoulder_r": fwd(36 - 30 * k),
            "elbow_l": X(-20), "elbow_r": X(-20), "_y": -0.04 * k}


# --- Crypt Warden -------------------------------------------------------------------------
def build_crypt_warden():
    """The crypt's keeper, set at its door when the crypt was new and never
    relieved: a tall knight in plate gone green with age, a closed helm with a
    light in the slit, a halberd as tall as it is and a lantern at its belt
    that burns the same cold colour as its eyes."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.70))
    r.add("hips", E(0.20, 0.15, 0.12), "warden_iron", "pelvis")
    humanoid_legs(r, -0.04, 0.12, 0.32, 0.32, 0.08, "warden_iron", "warden_iron_dk", foot_col="warden_iron_dk")
    for side in ("l", "r"):
        r.add("kneeguard", E(0.07, 0.07, 0.06), "verdigris", "knee_" + side, loc=(0, -0.05, 0.0))
    r.add("tasset", C(0.19, 0.23, 0.16, squash_y=0.8), "warden_iron", "pelvis", loc=(0, 0, 0.02))
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(4, 0, 0))
    r.add("cuirass", E(0.24, 0.17, 0.26), "warden_iron", "chest", loc=(0, 0, 0.26))
    r.add("plackart", E(0.16, 0.10, 0.12), "verdigris", "chest", loc=(0, -0.10, 0.18))
    r.add("gorget", E(0.13, 0.11, 0.06), "warden_iron_dk", "chest", loc=(0, 0, 0.50))
    for k in range(3):
        r.add("cloak", E(0.10, 0.04, 0.26), "warden_cloak", "chest", loc=(-0.14 + k * 0.14, 0.16, 0.16 - (k % 2) * 0.05))
    humanoid_arms(r, 0.44, 0.26, 0.27, 0.26, 0.065, "warden_iron", "warden_iron_dk", flare=10)
    for side in ("l", "r"):
        r.add("pauldron", E(0.13, 0.12, 0.09), "warden_iron", "shoulder_" + side, loc=(0, 0, 0.03))
        r.add("rim", E(0.14, 0.13, 0.03), "verdigris", "shoulder_" + side, loc=(0, 0, -0.03))
        r.add("gauntlet", E(0.06, 0.06, 0.065), "warden_iron_dk", "hand_" + side)
    r.joint("neck", (0, 0, 0.50), "chest")
    r.joint("head", (0, -0.01, 0.06), "neck", rest=(-6, 0, 0))
    r.add("helm", E(0.12, 0.13, 0.15), "warden_iron", "head", loc=(0, 0, 0.09))
    r.add("helmtop", E(0.10, 0.11, 0.06), "verdigris", "head", loc=(0, 0.01, 0.20))
    r.add("slit", E(0.09, 0.03, 0.016), "shroud_dk", "head", loc=(0, -0.12, 0.10))
    for sx in (-1, 1):
        r.add("glint", E(0.02, 0.01, 0.012), "warden_glow", "head", loc=(sx * 0.035, -0.14, 0.10))
    for k in range(4):
        r.limb("crest", (0, -0.06 + k * 0.05, 0.24), (0, -0.02 + k * 0.07, 0.40 - k * 0.03), 0.028, "warden_cloak",
               "head", r_tip=0.01)
    # The halberd, planted.
    r.limb("haft", (0, 0, 1.00), (0, 0, -0.64), 0.03, "driftwood_dk", "hand_r", r_tip=0.028)
    r.limb("axe", (0, 0.0, 0.86), (0, -0.16, 0.80), 0.07, "warden_blade", "hand_r", r_tip=0.07)
    r.add("axeedge", E(0.025, 0.05, 0.12), "warden_blade", "hand_r", loc=(0, -0.20, 0.82))
    r.limb("spike", (0, 0, 0.98), (0, 0, 1.22), 0.035, "warden_blade", "hand_r", r_tip=0.006)
    r.limb("beak", (0, 0.02, 0.86), (0, 0.12, 0.80), 0.03, "warden_blade", "hand_r", r_tip=0.006)
    # The lantern at its belt.
    r.limb("lanternhook", (-0.20, -0.06, 0.04), (-0.22, -0.08, -0.08), 0.012, "iron_dk", "pelvis", r_tip=0.01)
    r.add("lantern", E(0.05, 0.05, 0.07), "iron_dk", "pelvis", loc=(-0.22, -0.08, -0.14))
    r.add("lanternlight", E(0.035, 0.035, 0.045), "warden_glow", "pelvis", loc=(-0.22, -0.11, -0.14))
    return r.fit("crypt_warden", 56, WARDEN_REST)


# Straight up: 4 (chest) - 6 (shoulder) - 15 - 45 (elbow) + 62 = 0.
WARDEN_REST = {"shoulder_r": fwd(6), "elbow_r": X(-45), "hand_r": X(62), "shoulder_l": fwd(10), "elbow_l": X(-20)}


def warden_idle(t):
    s = sn(t)
    v = dict(WARDEN_REST)
    v.update({"_z": 0.004 * s, "chest": X(1.5 * s), "head": (0, 0, 3 * sn(t, 0.3))})
    return v


def warden_walk(t):
    v = gait(t, 24, 22, 0, 0.02, 2)
    v.update(WARDEN_REST)
    v.update({"shoulder_l": fwd(10 - 12 * sn(t))})
    return v


def warden_attack(t):
    # The halberd comes up over the helm in both hands and down on you.
    lift = {"shoulder_r": fwd(-160), "elbow_r": X(-30), "hand_r": X(-10), "shoulder_l": fwd(-150),
            "elbow_l": X(-40), "chest": X(-10), "_y": 0.03}
    chop = {"shoulder_r": fwd(60), "elbow_r": X(-6), "hand_r": X(-30), "shoulder_l": fwd(56), "elbow_l": X(-10),
            "chest": X(22), "_y": -0.10, "hip_l": fwd(26), "hip_r": fwd(-16)}
    return swing(t, WARDEN_REST, lift, chop, (0.44, 0.60, 1.0))


def warden_hurt(t):
    v = dict(WARDEN_REST)
    v.update(struck(math.sin(t * math.pi) * 0.7))
    return v


def warden_death(t):
    return topple(t, armed=True)


# --- Lord Ashcroft ----------------------------------------------------------------------
def build_vampire_lord():
    """The lord of the house under the graveyard, still dressed for dinner:
    tall, pale, the hair slicked back to a point, a crimson doublet with gold
    at the cuffs and a white stock at the throat, and a cape -- black outside,
    blood-red in -- with a collar that stands up behind his head. A rapier,
    because he would not be seen with anything heavier."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.72))
    r.add("hips", E(0.15, 0.12, 0.11), "vamp_boot", "pelvis")
    humanoid_legs(r, -0.03, 0.09, 0.33, 0.33, 0.055, "vamp_boot", "vamp_boot", foot_col="vamp_boot")
    for side in ("l", "r"):
        r.add("cuff", E(0.06, 0.06, 0.04), "vamp_boot_lt", "knee_" + side, loc=(0, 0, -0.02))
    r.add("coattail", C(0.16, 0.19, 0.18, squash_y=0.8), "vamp_doublet", "pelvis", loc=(0, 0.02, 0.02))
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(0, 0, 0))
    r.add("doublet", E(0.18, 0.13, 0.27), "vamp_doublet", "chest", loc=(0, 0, 0.24))
    r.add("front", E(0.08, 0.06, 0.22), "vamp_waist", "chest", loc=(0, -0.09, 0.22))
    for k in range(4):
        r.add("button", E(0.013, 0.01, 0.013), "gold", "chest", loc=(0, -0.145, 0.10 + k * 0.07))
    r.add("stock", E(0.06, 0.05, 0.07), "vamp_cravat", "chest", loc=(0, -0.10, 0.44))
    r.add("brooch", E(0.022, 0.012, 0.022), "vamp_eye_glow", "chest", loc=(0, -0.15, 0.42))
    humanoid_arms(r, 0.44, 0.20, 0.25, 0.25, 0.05, "vamp_doublet", "vamp_doublet", flare=10)
    for side in ("l", "r"):
        r.add("goldcuff", TORUS(0.05, 0.015), "gold", "elbow_" + side, loc=(0, 0, -0.20))
        r.add("hand", E(0.04, 0.035, 0.05), "vamp_skin", "hand_" + side)
        fingers(r, side, "vamp_skin", n=3, r_=0.011, reach=0.09, spread=0.03, down=0.05)
    # The cape: hung from the shoulders, with its lining and its collar.
    r.joint("cape", (0, 0.13, 0.46), "chest", rest=(6, 0, 0))
    r.add("cape", C(0.24, 0.32, 0.66, squash_y=0.22), "vamp_cape", "cape", loc=(0, 0.02, 0.0))
    r.add("lining", C(0.22, 0.30, 0.62, squash_y=0.18), "vamp_lining", "cape", loc=(0, -0.02, -0.02))
    for sx in (-1, 1):
        r.add("collar", E(0.13, 0.03, 0.17), "vamp_cape", "cape", loc=(sx * 0.14, 0.0, 0.14), rot=(0.2, sx * 0.5, sx * -0.35))
        r.add("collar_in", E(0.11, 0.02, 0.14), "vamp_lining", "cape", loc=(sx * 0.13, -0.03, 0.14),
              rot=(0.2, sx * 0.5, sx * -0.35))
    r.joint("neck", (0, -0.02, 0.50), "chest")
    r.joint("head", (0, -0.01, 0.06), "neck", rest=(-4, 0, 0))
    r.add("skull", E(0.095, 0.105, 0.12), "vamp_skin", "head", loc=(0, 0, 0.08))
    r.add("chin", E(0.05, 0.05, 0.04), "vamp_skin", "head", loc=(0, -0.06, -0.02))
    r.add("hair", E(0.10, 0.11, 0.07), "vamp_hair", "head", loc=(0, 0.03, 0.15))
    r.limb("peak", (0, -0.06, 0.18), (0, -0.10, 0.12), 0.03, "vamp_hair", "head", r_tip=0.006)
    r.add("back_hair", E(0.09, 0.07, 0.10), "vamp_hair", "head", loc=(0, 0.07, 0.06))
    for sx in (-1, 1):
        r.add("eye", E(0.02, 0.012, 0.015), "vamp_eye_glow", "head", loc=(sx * 0.038, -0.095, 0.09))
        r.limb("fang", (sx * 0.015, -0.085, -0.01), (sx * 0.015, -0.09, -0.05), 0.01, "tooth", "head", r_tip=0.003)
    # The rapier: a long thin blade, a swept hilt of gold.
    r.limb("blade", (0, 0, -0.06), (0, 0, -0.74), 0.018, "steel", "hand_r", r_tip=0.006)
    r.add("hilt", TORUS(0.05, 0.012), "gold", "hand_r", loc=(0, 0, -0.04), rot=(1.57, 0, 0))
    r.add("pommel", E(0.025, 0.025, 0.025), "gold", "hand_r", loc=(0, 0, 0.05))
    return r.fit("vampire_lord", 58, LORD_REST)


LORD_REST = {"shoulder_r": fwd(18), "elbow_r": X(-20), "hand_r": X(-40), "shoulder_l": (0, -20, 0),
             "elbow_l": X(-80), "hand_l": X(20)}


def lord_idle(t):
    s = sn(t)
    v = dict(LORD_REST)
    v.update({"_z": 0.004 * s, "chest": X(1.5 * s), "head": (0, 0, 4 * sn(t, 0.3)), "cape": (3 * s, 0, 0)})
    return v


def lord_walk(t):
    v = gait(t, 26, 22, 0, 0.02, 0)
    v.update(LORD_REST)
    v.update({"cape": (14 + 4 * sn(t * 2), 0, 3 * sn(t)), "shoulder_r": fwd(18 + 8 * sn(t))})
    return v


def lord_attack(t):
    # The cape thrown wide, and the rapier in.
    flare = {"cape": X(40), "~cape": 0.15, "shoulder_l": (0, -80, 0), "elbow_l": X(-10), "shoulder_r": fwd(-40),
             "elbow_r": X(-80), "hand_r": X(-60), "chest": (-6, 0, -16), "_y": 0.03}
    lunge = {"cape": X(26), "~cape": 0.10, "shoulder_l": (0, -60, 0), "shoulder_r": fwd(88), "elbow_r": X(0),
             "hand_r": X(0), "chest": (14, 0, 20), "_y": -0.16, "hip_l": fwd(40), "hip_r": fwd(-30),
             "knee_r": X(20)}
    return swing(t, LORD_REST, flare, lunge, (0.40, 0.55, 1.0))


def lord_hurt(t):
    v = dict(LORD_REST)
    v.update(struck(math.sin(t * math.pi), cape=X(20)))
    return v


def lord_death(t):
    # Down on one knee first, and then all the way.
    k = ease(min(1.0, t * 1.6))
    v = {"hip_l": fwd(70 * k), "knee_l": X(80 * k), "hip_r": fwd(-10 * k), "knee_r": X(90 * k),
         "_z": -0.18 * k, "chest": X(20 * k), "cape": X(10 * k)}
    if t > 0.5:
        w = topple((t - 0.5) * 2.0, armed=True)
        for key, val in w.items():
            if key == "_straighten":
                v[key] = val
            elif isinstance(val, tuple):
                base = v.get(key, (0.0, 0.0, 0.0))
                v[key] = tuple(a + b for a, b in zip(base, val))
            else:
                v[key] = v.get(key, 0.0) + val
    return v


# =================================================================================
#  Past the crypt: the three at the top of the ladder
# =================================================================================

# --- Revenant --------------------------------------------------------------------------
def build_revenant():
    """A knight come back for something, in bronze armour cracked across the
    chest with fire showing through the cracks, a torn cloak, a helm open on a
    face that is mostly skull, and the broken half of a greatsword dragged
    point down."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.64))
    r.add("hips", E(0.18, 0.14, 0.12), "rev_armour_dk", "pelvis")
    humanoid_legs(r, -0.04, 0.11, 0.29, 0.29, 0.07, "rev_armour", "rev_armour_dk", foot_col="rev_armour_dk")
    r.add("tasset", C(0.17, 0.21, 0.14, squash_y=0.8), "rev_armour", "pelvis", loc=(0, 0, 0.02))
    r.add("belt", TORUS(0.17, 0.02), "rev_bronze", "pelvis", loc=(0, 0, 0.06))
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(10, 0, 0))
    r.add("cuirass", E(0.22, 0.16, 0.24), "rev_armour", "chest", loc=(0, 0, 0.24))
    for x, z, rot in ((-0.06, 0.28, 0.6), (0.05, 0.20, -0.5), (0.10, 0.32, 0.2)):
        r.add("crack", E(0.07, 0.012, 0.012), "rev_glow", "chest", loc=(x, -0.155, z), rot=(0, rot, 0))
    r.add("core", E(0.035, 0.02, 0.035), "rev_glow", "chest", loc=(0, -0.16, 0.26))
    for k in range(3):
        r.add("cloak", E(0.09, 0.04, 0.25), "rev_cloak", "chest", loc=(-0.13 + k * 0.13, 0.15, 0.14 - (k % 2) * 0.07))
    humanoid_arms(r, 0.42, 0.24, 0.26, 0.25, 0.06, "rev_armour", "rev_armour_dk", flare=12)
    for side in ("l", "r"):
        r.add("pauldron", E(0.12, 0.11, 0.08), "rev_armour", "shoulder_" + side, loc=(0, 0, 0.03))
        r.add("pauldron_rim", E(0.125, 0.115, 0.025), "rev_bronze", "shoulder_" + side, loc=(0, 0, -0.03))
        r.add("gauntlet", E(0.055, 0.055, 0.06), "rev_bronze", "hand_" + side)
    r.joint("neck", (0, -0.02, 0.48), "chest", rest=(6, 0, 0))
    r.joint("head", (0, -0.01, 0.06), "neck", rest=(-10, 0, 0))
    r.add("helm", E(0.12, 0.125, 0.13), "rev_armour", "head", loc=(0, 0.02, 0.10))
    r.add("brim", E(0.125, 0.13, 0.025), "rev_bronze", "head", loc=(0, 0.0, 0.13))
    r.add("face", E(0.08, 0.05, 0.085), "bone_w", "head", loc=(0, -0.085, 0.05))
    for sx in (-1, 1):
        r.add("socket", E(0.03, 0.02, 0.026), "shroud_dk", "head", loc=(sx * 0.035, -0.125, 0.07))
        r.add("eye", E(0.022, 0.012, 0.02), "rev_glow", "head", loc=(sx * 0.035, -0.14, 0.07))
    r.add("teeth", E(0.045, 0.012, 0.014), "bone_g", "head", loc=(0, -0.13, 0.0))
    for k in range(4):
        r.limb("plume", (0, 0.02 + k * 0.03, 0.22), (0, 0.10 + k * 0.06, 0.30 - k * 0.06), 0.03, "rev_cloak",
               "head", r_tip=0.01)
    # The broken greatsword, point down.
    r.limb("grip", (0, 0, 0.10), (0, 0, -0.04), 0.022, "leather_dk", "hand_r", r_tip=0.022)
    r.add("guard", E(0.12, 0.03, 0.025), "rev_armour_dk", "hand_r", loc=(0, 0, -0.06))
    r.limb("blade", (0, 0, -0.06), (0, 0, -0.50), 0.045, "rev_blade", "hand_r", r_tip=0.042)
    r.limb("break", (0.0, 0, -0.50), (0.03, 0, -0.56), 0.04, "rev_blade", "hand_r", r_tip=0.01)
    return r.fit("revenant", 44, REV_REST)


REV_REST = {"shoulder_r": fwd(6), "elbow_r": X(-8), "hand_r": X(-8), "shoulder_l": fwd(12), "elbow_l": X(-24)}


def rev_idle(t):
    s = sn(t)
    v = dict(REV_REST)
    v.update({"_z": 0.005 * s, "chest": X(10 + 2 * s), "head": (0, 0, 5 * sn(t, 0.3))})
    return v


def rev_walk(t):
    v = gait(t, 28, 26, 0, 0.025, 12)
    v.update(REV_REST)
    v.update({"shoulder_l": fwd(12 - 16 * sn(t))})
    return v


def rev_attack(t):
    # Up over the shoulder in both hands, and down.
    lift = {"shoulder_r": fwd(-150), "elbow_r": X(-40), "hand_r": X(-20), "shoulder_l": fwd(-140),
            "elbow_l": X(-50), "chest": (-10, 0, -16), "_y": 0.03}
    cleave = {"shoulder_r": fwd(60), "elbow_r": X(-6), "hand_r": X(-10), "shoulder_l": fwd(56), "elbow_l": X(-10),
              "chest": (24, 0, 16), "_y": -0.12, "hip_l": fwd(26), "hip_r": fwd(-16)}
    return swing(t, REV_REST, lift, cleave, (0.42, 0.58, 1.0))


def rev_hurt(t):
    v = dict(REV_REST)
    v.update(struck(math.sin(t * math.pi)))
    return v


def rev_death(t):
    return topple(t, armed=True)


# --- Abyssal Demon -----------------------------------------------------------------------
def build_abyssal_demon():
    """Something from further down than the Pit Lord's kind: long and thin and
    the colour of the dark between stars, with the fire showing through it in
    seams, horns that sweep back and then forward, arms to its knees ending in
    claws like sickles, and a barbed tail it carries up behind it like a scorpion."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.80))
    r.add("hips", E(0.18, 0.14, 0.13), "abyss_dk", "pelvis")
    humanoid_legs(r, -0.05, 0.12, 0.38, 0.42, 0.075, "abyss", "abyss_dk", digitigrade=True, foot_col="abyss_horn")
    for side in ("l", "r"):
        r.limb("spur", (0, 0.03, -0.02), (0, 0.14, 0.04), 0.03, "abyss_horn", "knee_" + side, r_tip=0.006)
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(14, 0, 0))
    r.add("torso", E(0.25, 0.17, 0.30), "abyss", "chest", loc=(0, 0, 0.30))
    r.add("waist", E(0.14, 0.11, 0.14), "abyss_dk", "chest", loc=(0, 0, 0.06))
    for x, z, rot in ((-0.08, 0.34, 0.9), (0.06, 0.24, -0.8), (0.12, 0.40, 0.4), (-0.12, 0.18, -0.3)):
        r.add("seam", E(0.08, 0.012, 0.012), "abyss_glow", "chest", loc=(x, -0.15, z), rot=(0, rot, 0))
    r.add("heart", E(0.05, 0.03, 0.05), "abyss_glow", "chest", loc=(0, -0.16, 0.32))
    for k in range(5):
        r.limb("spine", (0, 0.14, 0.50 - k * 0.10), (0, 0.26, 0.58 - k * 0.10), 0.035, "abyss_horn", "chest",
               r_tip=0.006)
    humanoid_arms(r, 0.52, 0.28, 0.40, 0.40, 0.07, "abyss", "abyss_dk", flare=18)
    for side in ("l", "r"):
        for k in range(3):
            r.limb("claw", (0, -0.03, -0.05), ((k - 1) * 0.07, -0.16, -0.26), 0.03, "abyss_horn", "hand_" + side,
                   r_tip=0.005)
    r.joint("neck", (0, -0.04, 0.62), "chest", rest=(-6, 0, 0))
    r.joint("head", (0, -0.02, 0.06), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.13, 0.14, 0.14), "abyss", "head", loc=(0, 0, 0.09))
    r.add("jaw", E(0.11, 0.11, 0.06), "abyss_dk", "head", loc=(0, -0.07, -0.02))
    r.add("maw", E(0.07, 0.03, 0.02), "abyss_glow", "head", loc=(0, -0.15, 0.0))
    for sx in (-1, 1):
        r.add("eye", E(0.04, 0.02, 0.022), "abyss_glow", "head", loc=(sx * 0.06, -0.12, 0.11))
        r.limb("horn", (sx * 0.09, 0.02, 0.18), (sx * 0.26, 0.20, 0.28), 0.06, "abyss_horn", "head", r_tip=0.04)
        r.limb("horn2", (sx * 0.26, 0.20, 0.28), (sx * 0.34, 0.04, 0.50), 0.04, "abyss_horn", "head", r_tip=0.005)
        r.limb("tusk", (sx * 0.06, -0.12, -0.04), (sx * 0.08, -0.16, 0.06), 0.018, "tooth", "head", r_tip=0.004)
    r.joint("tail1", (0, 0.14, -0.04), "pelvis", rest=(100, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.40), 0.06, "abyss", "tail1", r_tip=0.045)
    r.joint("tail2", (0, 0, -0.40), "tail1", rest=(50, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.38), 0.045, "abyss_dk", "tail2", r_tip=0.03)
    r.joint("tail3", (0, 0, -0.38), "tail2", rest=(40, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.26), 0.03, "abyss_dk", "tail3", r_tip=0.02)
    r.limb("barb", (0, 0, -0.26), (0, 0, -0.44), 0.06, "abyss_glow", "tail3", r_tip=0.005)
    return r.fit("abyssal_demon", 62)


def abyss_idle(t):
    v = cr.demon_idle(t)
    v.update({"tail2": (0, 0, 8 * sn(t, 0.2)), "tail3": (0, 0, 12 * sn(t, 0.4))})
    return v


def abyss_walk(t):
    v = cr.demon_walk(t)
    v.update({"tail2": (0, 0, 14 * sn(t, 0.2)), "tail3": (0, 0, 18 * sn(t, 0.4))})
    return v


def abyss_attack(t):
    # The claws, and the tail over its head behind them.
    v = cr.demon_attack(t)
    i, k = phases(t, 0.42, 0.6, 1.0)
    sting = [k, 1.0, 1.0 - k, 0.0][i]
    v.update({"tail1": (-30 * sting, 0, 0), "tail2": (-30 * sting, 0, 0), "tail3": (-20 * sting, 0, 0)})
    return v


# --- Rime Revenant -----------------------------------------------------------------------
def build_rime_revenant():
    """A giant who went up the Ice Spire in armour and did not come down, and
    has been standing in the cold since: plate rimed white, icicles hanging
    off every edge of it, a helm with horns of ice, eyes like the light under
    a glacier, and a greataxe frozen to its hand."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.62))
    r.add("hips", E(0.30, 0.22, 0.18), "rime_armour_dk", "pelvis")
    r.add("tasset", C(0.30, 0.34, 0.20, squash_y=0.8), "rime_armour", "pelvis", loc=(0, 0, 0.02))
    humanoid_legs(r, -0.06, 0.18, 0.28, 0.28, 0.12, "rime_armour", "rime_armour_dk", foot_col="rime_armour_dk")
    for k in range(4):
        r.limb("icicle", (-0.24 + k * 0.16, -0.24, -0.14), (-0.24 + k * 0.16, -0.26, -0.32 - (k % 2) * 0.06), 0.03,
               "ice", "pelvis", r_tip=0.004)
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(12, 0, 0))
    r.add("cuirass", E(0.42, 0.30, 0.40), "rime_armour", "chest", loc=(0, 0, 0.34))
    r.add("frost", E(0.22, 0.12, 0.08), "rime_frost", "chest", loc=(0, 0.10, 0.66))
    r.add("gorget", E(0.24, 0.20, 0.08), "rime_armour_dk", "chest", loc=(0, -0.04, 0.68))
    r.add("plate", E(0.26, 0.14, 0.20), "rime_armour_dk", "chest", loc=(0, -0.18, 0.26))
    r.add("core", E(0.05, 0.03, 0.05), "rime_glow", "chest", loc=(0, -0.31, 0.32))
    for sx in (-1, 1):
        for k in range(3):
            r.limb("crystal", (sx * (0.20 + k * 0.08), 0.06, 0.62 - k * 0.04), (sx * (0.30 + k * 0.10), 0.14, 0.82 - k * 0.08),
                   0.05, "ice", "chest", r_tip=0.008)
    humanoid_arms(r, 0.58, 0.40, 0.38, 0.38, 0.14, "rime_armour", "rime_armour_dk", flare=10)
    for side in ("l", "r"):
        r.add("pauldron", E(0.19, 0.17, 0.12), "rime_armour", "shoulder_" + side, loc=(0, 0, 0.03))
        r.add("rime", E(0.14, 0.12, 0.05), "rime_frost", "shoulder_" + side, loc=(0, 0.02, 0.12))
        r.add("fist", E(0.13, 0.13, 0.13), "rime_armour_dk", "hand_" + side)
        for k in range(2):
            r.limb("icicle", (0.05 - k * 0.1, 0, -0.30), (0.05 - k * 0.1, 0.02, -0.46), 0.03, "ice", "elbow_" + side,
                   r_tip=0.004)
    r.joint("neck", (0, -0.14, 0.72), "chest")
    r.joint("head", (0, -0.04, 0.06), "neck", rest=(-18, 0, 0))
    r.add("helm", E(0.20, 0.20, 0.19), "rime_armour", "head", loc=(0, 0, 0.10))
    r.add("visor", E(0.15, 0.04, 0.03), "shroud_dk", "head", loc=(0, -0.19, 0.11))
    r.add("beard", E(0.14, 0.10, 0.10), "rime_frost", "head", loc=(0, -0.14, -0.04))
    for sx in (-1, 1):
        r.add("eye", E(0.035, 0.015, 0.02), "rime_glow", "head", loc=(sx * 0.07, -0.21, 0.11))
        r.limb("horn", (sx * 0.16, 0, 0.16), (sx * 0.34, 0.06, 0.30), 0.06, "ice", "head", r_tip=0.03)
        r.limb("horn2", (sx * 0.34, 0.06, 0.30), (sx * 0.36, -0.06, 0.50), 0.04, "ice_dk", "head", r_tip=0.005)
        for k in range(2):
            r.limb("icicle", (sx * (0.04 + k * 0.06), -0.18, -0.10), (sx * (0.04 + k * 0.06), -0.19, -0.24), 0.022,
                   "ice", "head", r_tip=0.003)
    # The greataxe, frozen to its hand, hanging head down.
    r.limb("haft", (0, 0, 0.10), (0, 0, -0.46), 0.045, "rime_armour_dk", "hand_r", r_tip=0.04)
    r.add("axehead", E(0.05, 0.19, 0.15), "ice", "hand_r", loc=(0, -0.14, -0.38))
    r.add("axecore", E(0.04, 0.12, 0.10), "ice_dk", "hand_r", loc=(0, -0.08, -0.38))
    r.add("counter", E(0.04, 0.09, 0.07), "ice", "hand_r", loc=(0, 0.08, -0.40))
    return r.fit("rime_revenant", 60)


# =================================================================================
#  The Brimstone Palace's master
# =================================================================================

# --- The Cinder King ---------------------------------------------------------------
def build_cinder_king():
    """The lord of the Brimstone Palace: a demon half again the height of the
    Abyssal kind, his hide the dark red of a coal with the fire showing through
    it in cracks, in black plate trimmed with gold, a mantle of black lined in
    crimson off his shoulders, ram's horns and a crown of burning gold between
    them, and a greatsword of black iron with an edge that glows."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.80))
    r.add("hips", E(0.22, 0.17, 0.15), "king_armour", "pelvis")
    humanoid_legs(r, -0.05, 0.14, 0.38, 0.42, 0.10, "king_skin", "king_skin_dk", digitigrade=True,
                  foot_col="king_horn")
    for side in ("l", "r"):
        r.add("greave", E(0.09, 0.09, 0.16), "king_armour", "knee_" + side, loc=(0, -0.03, -0.16))
        r.limb("knee_spike", (0, -0.06, 0.0), (0, -0.16, 0.08), 0.035, "king_gold", "knee_" + side, r_tip=0.006)
    # The skirt of plates, and the belt with a skull for a buckle.
    r.add("tasset", C(0.22, 0.26, 0.18, squash_y=0.8), "king_armour", "pelvis", loc=(0, 0, 0.02))
    r.add("belt", TORUS(0.22, 0.03), "king_gold", "pelvis", loc=(0, 0, 0.06))
    r.add("buckle", E(0.06, 0.04, 0.06), "bone_w", "pelvis", loc=(0, -0.22, 0.06))
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(8, 0, 0))
    r.add("torso", E(0.33, 0.22, 0.34), "king_skin", "chest", loc=(0, 0, 0.32))
    r.add("breastplate", E(0.30, 0.20, 0.24), "king_armour", "chest", loc=(0, -0.04, 0.38))
    r.add("plate_trim", E(0.31, 0.205, 0.035), "king_gold", "chest", loc=(0, -0.04, 0.24))
    r.add("heart", E(0.05, 0.03, 0.06), "king_glow", "chest", loc=(0, -0.235, 0.40))
    for x, z, rot in ((-0.12, 0.14, 0.7), (0.14, 0.10, -0.6), (0.20, 0.22, 0.3)):
        r.add("crack", E(0.06, 0.012, 0.012), "king_glow", "chest", loc=(x, -0.19, z), rot=(0, rot, 0))
    # The mantle: black, crimson inside, a collar of spikes.
    r.joint("cape", (0, 0.16, 0.56), "chest", rest=(8, 0, 0))
    r.add("mantle", C(0.30, 0.40, 0.78, squash_y=0.22), "king_mantle", "cape", loc=(0, 0.02, 0.0))
    r.add("lining", C(0.28, 0.38, 0.74, squash_y=0.18), "king_lining", "cape", loc=(0, -0.02, -0.02))
    for k in range(5):
        a = math.radians(-60 + k * 30)
        r.limb("collar_spike", (math.sin(a) * 0.26, 0.02, 0.06), (math.sin(a) * 0.40, 0.10, 0.30), 0.035,
               "king_armour_lt", "cape", r_tip=0.006)
    humanoid_arms(r, 0.54, 0.34, 0.36, 0.36, 0.09, "king_skin", "king_skin_dk", flare=14)
    for side in ("l", "r"):
        r.add("pauldron", E(0.17, 0.16, 0.12), "king_armour", "shoulder_" + side, loc=(0, 0, 0.04))
        r.add("pauldron_trim", E(0.175, 0.165, 0.03), "king_gold", "shoulder_" + side, loc=(0, 0, -0.03))
        for k in range(3):
            r.limb("shoulder_spike", ((k - 1) * 0.07, 0.02, 0.10), ((k - 1) * 0.10, 0.06, 0.30 - abs(k - 1) * 0.06),
                   0.035, "king_armour_lt", "shoulder_" + side, r_tip=0.006)
        r.add("vambrace", E(0.08, 0.08, 0.14), "king_armour", "elbow_" + side, loc=(0, 0, -0.20))
        for k in range(3):
            r.limb("claw", (0, -0.04, -0.06), ((k - 1) * 0.05, -0.10, -0.18), 0.025, "king_horn", "hand_" + side,
                   r_tip=0.005)
    r.joint("neck", (0, -0.04, 0.64), "chest", rest=(-4, 0, 0))
    r.joint("head", (0, -0.02, 0.07), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.15, 0.16, 0.16), "king_skin", "head", loc=(0, 0, 0.10))
    r.add("jaw", E(0.13, 0.12, 0.07), "king_skin_dk", "head", loc=(0, -0.07, -0.01))
    r.add("brow", E(0.14, 0.06, 0.04), "king_skin_dk", "head", loc=(0, -0.12, 0.15))
    r.add("maw", E(0.07, 0.03, 0.02), "king_glow", "head", loc=(0, -0.155, 0.02))
    for sx in (-1, 1):
        r.add("eye", E(0.04, 0.02, 0.022), "king_glow", "head", loc=(sx * 0.06, -0.14, 0.12))
        # Ram's horns: back, round and forward again.
        r.limb("horn", (sx * 0.12, 0.02, 0.18), (sx * 0.30, 0.18, 0.24), 0.07, "king_horn", "head", r_tip=0.055)
        r.limb("horn2", (sx * 0.30, 0.18, 0.24), (sx * 0.36, 0.06, 0.06), 0.055, "king_horn", "head", r_tip=0.04)
        r.limb("horn3", (sx * 0.36, 0.06, 0.06), (sx * 0.32, -0.10, 0.10), 0.04, "king_armour_lt", "head",
               r_tip=0.006)
        r.limb("tusk", (sx * 0.06, -0.13, -0.04), (sx * 0.08, -0.17, 0.05), 0.018, "tooth", "head", r_tip=0.004)
    # The crown, burning.
    r.add("circlet", TORUS(0.13, 0.025), "king_gold", "head", loc=(0, 0.0, 0.22))
    for k in range(5):
        a = math.radians(-60 + k * 30)
        r.limb("crown_pt", (math.sin(a) * 0.12, -math.cos(a) * 0.12 + 0.03, 0.22),
               (math.sin(a) * 0.13, -math.cos(a) * 0.13 + 0.03, 0.36 + (0.06 if k == 2 else 0)), 0.03, "king_gold",
               "head", r_tip=0.006)
        r.add("crown_fire", E(0.025, 0.025, 0.05), "king_glow", "head",
              loc=(math.sin(a) * 0.13, -math.cos(a) * 0.13 + 0.03, 0.40 + (0.06 if k == 2 else 0)))
    # The greatsword, point down at his side.
    r.limb("grip", (0, 0, 0.12), (0, 0, -0.06), 0.028, "king_armour", "hand_r", r_tip=0.028)
    r.add("pommel", E(0.045, 0.045, 0.045), "king_gold", "hand_r", loc=(0, 0, 0.14))
    r.add("guard", E(0.20, 0.05, 0.04), "king_gold", "hand_r", loc=(0, 0, -0.08))
    for sx in (-1, 1):
        r.limb("quillon", (sx * 0.18, 0, -0.08), (sx * 0.26, 0, 0.0), 0.025, "king_gold", "hand_r", r_tip=0.006)
    r.limb("blade", (0, 0, -0.10), (0, 0, -0.96), 0.07, "king_blade", "hand_r", r_tip=0.03)
    r.limb("edge", (0.05, -0.02, -0.14), (0.05, -0.02, -0.92), 0.016, "king_glow", "hand_r", r_tip=0.006)
    r.limb("edge2", (-0.05, -0.02, -0.14), (-0.05, -0.02, -0.92), 0.016, "king_glow", "hand_r", r_tip=0.006)
    return r.fit("cinder_king", 84, KING_REST)


KING_REST = {"shoulder_r": fwd(8), "elbow_r": X(-16), "hand_r": X(-10), "shoulder_l": fwd(14), "elbow_l": X(-40)}


def king_idle(t):
    s = sn(t)
    v = dict(KING_REST)
    v.update({"_z": 0.006 * s, "chest": X(8 + 2 * s), "head": (0, 0, 4 * sn(t, 0.3)), "cape": (8 + 3 * s, 0, 0),
              "shoulder_l": fwd(14 + 5 * s)})
    return v


def king_walk(t):
    v = gait(t, 26, 26, 0, 0.03, 8)
    v.update(KING_REST)
    v.update({"cape": (18 + 5 * sn(t * 2), 0, 4 * sn(t)), "shoulder_l": fwd(14 - 14 * sn(t)),
              "shoulder_r": fwd(8 + 6 * sn(t)), "pelvis": (0, 5 * sn(t), 0)})
    return v


def king_attack(t):
    # Up over his head in both hands, and down through whatever is in front.
    lift = {"shoulder_r": fwd(-160), "elbow_r": X(-30), "hand_r": X(-10), "shoulder_l": fwd(-150),
            "elbow_l": X(-40), "chest": (-12, 0, -14), "cape": X(14), "_y": 0.03}
    cleave = {"shoulder_r": fwd(58), "elbow_r": X(-6), "hand_r": X(-10), "shoulder_l": fwd(52), "elbow_l": X(-10),
              "chest": (26, 0, 16), "cape": X(4), "_y": -0.12, "_z": -0.02, "hip_l": fwd(28), "hip_r": fwd(-18)}
    return swing(t, KING_REST, lift, cleave, (0.44, 0.60, 1.0))


def king_hurt(t):
    v = dict(KING_REST)
    v.update(struck(math.sin(t * math.pi) * 0.8, cape=X(16)))
    return v


def king_death(t):
    # To his knees, the sword let go, and then forward onto his face.
    k = ease(min(1.0, t * 1.6))
    v = {"hip_l": fwd(70 * k), "knee_l": X(80 * k), "hip_r": fwd(-10 * k), "knee_r": X(90 * k),
         "_z": -0.18 * k, "chest": X(20 * k), "cape": X(10 * k), "shoulder_r": fwd(-10 * k)}
    if t > 0.5:
        w = topple((t - 0.5) * 2.0, dir=-1, armed=True)
        for key, val in w.items():
            if key == "_straighten":
                v[key] = val
            elif isinstance(val, tuple):
                base = v.get(key, (0.0, 0.0, 0.0))
                v[key] = tuple(a + b for a, b in zip(base, val))
            else:
                v[key] = v.get(key, 0.0) + val
    return v


# =================================================================================
#  Registration
# =================================================================================

# id: (builder, frame px, (idle, walk, attack, hurt, death), shadow radius)
CREATURES = {
    "bog_lurker":      (build_bog_lurker, 64,
                        (lurker_idle, lurker_walk, lurker_attack, lurker_hurt, lurker_death), 0.50),
    "mire_croaker":    (build_mire_croaker, 64,
                        (croak_idle, croak_walk, croak_attack, croak_hurt, croak_death), 0.52),
    "swamp_hag":       (build_swamp_hag, 80,
                        (hag_idle, hag_walk, hag_attack, hag_hurt, hag_death), 0.30),
    "rot_shambler":    (build_rot_shambler, 64,
                        (shamble_idle, shamble_walk, shamble_attack, shamble_hurt, shamble_death), 0.44),
    "fen_gator":       (build_fen_gator, 96,
                        (gator_idle, gator_walk, gator_attack, gator_hurt, gator_death), 0.56),
    "fen_stalker":     (build_fen_stalker, 64,
                        (stalk_idle, stalk_walk, stalk_attack, stalk_hurt, stalk_death), 0.40),
    "drowned_one":     (build_drowned_one, 64,
                        (drowned_idle, drowned_walk, drowned_attack, drowned_hurt, drowned_death), 0.32),
    "witchlight":      (build_witchlight, 48,
                        (wisp_idle, wisp_walk, wisp_attack, wisp_hurt, wisp_death), 0.20),
    "lizard_shaman":   (build_lizard_shaman, 96,
                        (shaman_idle, shaman_walk, shaman_attack, shaman_hurt, shaman_death), 0.50),
    "bayou_matriarch": (build_bayou_matriarch, 160,
                        (mother_idle, mother_walk, mother_attack, mother_hurt, mother_death), 0.80),
    "grave_ghoul":     (build_grave_ghoul, 64,
                        (ghoul_idle, ghoul_walk, ghoul_attack, ghoul_hurt, ghoul_death), 0.30),
    "bone_archer":     (build_bone_archer, 64,
                        (archer_idle, archer_walk, archer_attack, archer_hurt, archer_death), 0.30),
    "cryptbound":      (build_cryptbound, 64,
                        (bound_idle, bound_walk, bound_attack, bound_hurt, bound_death), 0.30),
    "bone_knight":     (build_bone_knight, 72,
                        (knight_idle, knight_walk, knight_attack, knight_hurt, knight_death), 0.34),
    "plague_corpse":   (build_plague_corpse, 64,
                        (plague_idle, plague_walk, plague_attack, plague_hurt, plague_death), 0.40),
    "tomb_shade":      (build_tomb_shade, 72,
                        (cr.banshee_idle, cr.banshee_walk, cr.banshee_attack, cr.banshee_hurt, cr.banshee_death),
                        0.26),
    "grave_hound":     (build_grave_hound, 64,
                        (cr.hound_idle, cr.hound_walk, cr.hound_attack, cr.hound_hurt, cr.hound_death), 0.36),
    "blood_thrall":    (build_blood_thrall, 64,
                        (thrall_idle, thrall_walk, thrall_attack, thrall_hurt, thrall_death), 0.30),
    "bone_colossus":   (build_bone_colossus, 112,
                        (cr.troll_idle, cr.troll_walk, cr.troll_attack, cr.troll_hurt, colossus_death), 0.70),
    "nosferatu":       (build_nosferatu, 72,
                        (nosf_idle, nosf_walk, nosf_attack, nosf_hurt, nosf_death), 0.32),
    "crypt_warden":    (build_crypt_warden, 96,
                        (warden_idle, warden_walk, warden_attack, warden_hurt, warden_death), 0.44),
    "vampire_lord":    (build_vampire_lord, 96,
                        (lord_idle, lord_walk, lord_attack, lord_hurt, lord_death), 0.44),
    "revenant":        (build_revenant, 80,
                        (rev_idle, rev_walk, rev_attack, rev_hurt, rev_death), 0.40),
    "abyssal_demon":   (build_abyssal_demon, 96,
                        (abyss_idle, abyss_walk, abyss_attack, cr.demon_hurt, cr.demon_death), 0.50),
    "rime_revenant":   (build_rime_revenant, 112,
                        (cr.troll_idle, cr.troll_walk, cr.troll_attack, cr.troll_hurt, cr.troll_death), 0.66),
    "cinder_king":     (build_cinder_king, 144,
                        (king_idle, king_walk, king_attack, king_hurt, king_death), 0.70),
}

# What swims *in* the water: drawn sunk to the waterline when afloat.
SWIMS = {"bog_lurker": lurker_swim, "fen_gator": gator_swim, "drowned_one": drowned_swim}


def register():
    cr.CREATURES.update(CREATURES)
    cr.SWIMMERS.update(SWIMS)
    cr.WATERLINE.update(SWIMS)
