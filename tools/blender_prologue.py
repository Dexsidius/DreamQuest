# =============================================================================
#  blender_prologue.py - the three figures the prologue needs that nothing else
#  in the game is: the stranger who takes the player (??? -- Vexel Von Finch),
#  the two suits of armour that wake in his foyer, and Vigil, the old prisoner
#  in the cell down the corridor.
#
#  Rendered by tools/make_creatures.ps1 like every other monster:
#      .\tools\make_creatures.ps1 -Only vexel,animated_armor,vigil
#
#  Built out of blender_creatures.py's parts and fitted to a height on screen
#  the way blender_bestiary.py's monsters are, and registered into the same
#  CREATURES table; blender_creatures.py hands itself over at its foot.
#
#  The Rig here is the bestiary's with two more kinds of key, both because the
#  stranger and the armour have to come apart in ways no other rig does:
#    "@joint": (x, y, z)   moves a joint off where it was built, in its parent's
#                          units; "&joint" the same in the figure's own axes --
#                          a helm falling off the suit it was on;
#    "%joint": (x, y, z)   scales a joint and what hangs on it unevenly --
#                          fingers fanned wider;
#    "_squash": (x, y, z)  scales the whole figure unevenly about its feet --
#                          the stranger folding down into his own robe;
#    "^r" / "^l": (x, y, z)  puts that hand there, in the chest's own space,
#                          by turning the shoulder and bending the elbow -- two
#                          hands on one hilt, or on the bars of a cell, are not
#                          somewhere angles set by eye ever quite reach;
#    "^r_aim": (x, y, z)   and turns the hand so what it holds points that way
#                          ("^r_edge" says which way the flat of it faces).
# =============================================================================

import math

import bpy
from mathutils import Matrix, Quaternion, Vector

import blender_character as bc
import blender_creatures as cr
import blender_bestiary as bb

E, C = cr.E, cr.C
X, fwd, sn, ease, phases, mix = cr.X, cr.fwd, cr.sn, cr.ease, cr.phases, cr.mix
gait, topple = cr.gait, cr.topple
humanoid_legs, humanoid_arms = cr.humanoid_legs, cr.humanoid_arms
TORUS = bc.mesh_torus
FRUSTUM = bc.mesh_frustum


# --- colours ---------------------------------------------------------------------------
bc.PALETTE.update({
    # The stranger: indigo over violet, gold gone dull, bone, and the light of
    # his own that comes from under the robe and out of the hourglass.
    "vex_robe": (0.20, 0.17, 0.38), "vex_robe_dk": (0.13, 0.11, 0.26), "vex_violet": (0.40, 0.25, 0.58),
    "vex_violet_lt": (0.52, 0.36, 0.70), "vex_gold": (0.70, 0.57, 0.30), "vex_gold_dk": (0.50, 0.40, 0.22),
    "vex_bone": (0.91, 0.89, 0.83), "vex_bone_dk": (0.70, 0.68, 0.64), "vex_void": (0.05, 0.03, 0.08),
    "vex_shade": (0.09, 0.07, 0.14), "vex_smoke": (0.25, 0.19, 0.36), "vex_smoke_dk": (0.16, 0.12, 0.24),
    "vex_eye_glow": (0.86, 0.76, 1.00), "vex_sand_glow": (0.80, 0.62, 1.00), "vex_under_glow": (0.46, 0.30, 0.74),
    # The armour in his foyer: steel blued almost black, the same dull gold
    # as his trim, his indigo on the tabard, and nothing inside.
    "arm_steel": (0.36, 0.42, 0.54), "arm_steel_dk": (0.22, 0.26, 0.36), "arm_steel_lt": (0.54, 0.61, 0.73),
    "arm_void": (0.04, 0.03, 0.06), "arm_blade": (0.72, 0.76, 0.82), "arm_blade_dk": (0.48, 0.52, 0.60),
    "arm_grip": (0.30, 0.20, 0.16),
    # Vigil: skin gone grey, rags gone the colour of the cell, white hair.
    "vig_skin": (0.69, 0.65, 0.61), "vig_skin_dk": (0.53, 0.50, 0.48), "vig_hollow": (0.42, 0.39, 0.38),
    "vig_rag": (0.45, 0.40, 0.33), "vig_rag_dk": (0.32, 0.28, 0.23), "vig_hair": (0.93, 0.92, 0.89),
    "vig_hair_dk": (0.76, 0.75, 0.72), "vig_eye": (0.12, 0.10, 0.11),
})


# --- the rig ---------------------------------------------------------------------------
class Rig(bb.Rig):
    """The bestiary's Rig, plus "@joint" (move a joint), "%joint" (scale one
    unevenly) and "_squash" (scale the whole figure unevenly about its feet)."""

    def __init__(self):
        super().__init__()
        self.home = {}

    def joint(self, name, loc, parent="pose", rest=(0, 0, 0)):
        self.home[name] = Vector(loc)
        return super().joint(name, loc, parent, rest)

    def reach(self, side, target, aim=None, edge=None, pole=None):
        """Two-bone IK in the chest's space: bend the elbow until shoulder to
        hand is as long as shoulder to `target`, swing the arm onto it with the
        elbow turned towards `pole` (out, back and down unless told), and turn
        the hand so its -Z -- the way a held blade points -- lies along `aim`."""
        sh, el, ha = "shoulder_" + side, "elbow_" + side, "hand_" + side
        sx = 1.0 if self.home[sh].x > 0 else -1.0
        upper, fore = self.home[el].length, self.home[ha].length
        to = Vector(target) - self.home[sh]
        d = max(abs(upper - fore) + 1e-4, min(to.length, (upper + fore) * 0.999))
        bend = math.acos(max(-1.0, min(1.0, (d * d - upper * upper - fore * fore) / (2 * upper * fore))))
        hand = Vector((0.0, -fore * math.sin(bend), -upper - fore * math.cos(bend)))
        q = hand.normalized().rotation_difference(to.normalized())
        # Twist about the line to the target until the elbow points at the pole.
        axis = to.normalized()
        elbow = q @ Vector((0.0, 0.0, -upper))
        want = Vector(pole) if pole else Vector((sx * 0.8, 0.45, -0.4))
        e_perp, w_perp = elbow - axis * elbow.dot(axis), want - axis * want.dot(axis)
        if e_perp.length > 1e-5 and w_perp.length > 1e-5:
            angle = e_perp.angle(w_perp)
            if e_perp.cross(w_perp).dot(axis) < 0:
                angle = -angle
            q = Quaternion(axis, angle) @ q
        self.j[sh].rotation_euler = q.to_euler("XYZ")
        self.j[el].rotation_euler = (-bend, 0.0, 0.0)
        if aim is None:
            return
        parent = q.to_matrix() @ Matrix.Rotation(-bend, 3, "X")
        z = -Vector(aim).normalized()
        x = Vector(edge) if edge else Vector((1.0, 0.0, 0.0))
        x = x - z * x.dot(z)
        if x.length < 1e-4:
            x = Vector((0.0, 1.0, 0.0)) - z * z.y
        x.normalize()
        y = z.cross(x)
        basis = Matrix((x, y, z)).transposed()
        self.j[ha].rotation_euler = (parent.inverted() @ basis).to_euler("XYZ")

    def apply(self, values, turn=0.0):
        super().apply(values, turn)
        for side in ("r", "l"):
            if ("^" + side) in values:
                self.reach(side, values["^" + side], values.get("^%s_aim" % side), values.get("^%s_edge" % side),
                           values.get("^%s_pole" % side))
        for key, value in values.items():
            if key.startswith("@") and key[1:] in self.j:
                self.j[key[1:]].location = self.home[key[1:]] + Vector(value)
            elif key.startswith("&") and key[1:] in self.j:
                # The same, but the offset is in the figure's own axes rather
                # than its parent's: a helm that falls off a body pitched
                # forward still falls down, and not down the body's back.
                bpy.context.view_layer.update()
                ob = self.j[key[1:]]
                rel = (self.pose.matrix_world.inverted() @ ob.parent.matrix_world).to_3x3()
                ob.location = self.home[key[1:]] + rel.inverted() @ Vector(value)
            elif key.startswith("%") and key[1:] in self.j:
                self.j[key[1:]].scale = value
        squash = values.get("_squash")
        if squash:
            s = self.pose.scale
            self.pose.scale = (s[0] * squash[0], s[1] * squash[1], s[2] * squash[2])


def scaled(ob, sx=1.0, sy=1.0, sz=1.0):
    ob.scale = (sx, sy, sz)
    return ob


# =================================================================================
#  ??? -- Vexel Von Finch
# =================================================================================
HEM_R, HEM_SQ = 0.42, 0.70        # the robe's hem: its radius, and how far it is flattened front to back


def build_vexel():
    """A sorcerer-king a head and more over anyone in the town: a tall pointed
    hood with a skull in the dark of it and two pale violet lights for eyes, a
    cracked crescent moon in dull gold on its brow, robes of indigo laid over
    violet that flare out to the floor and fray into smoke there, an hourglass
    of glowing sand on a chain at his chest, and long white fingers. No legs:
    the robe reaches the ground, and something faintly violet shines out from
    under it. Regal before monstrous -- the gold, the mantle and the stillness
    do that; the skull and the fingers do the rest."""
    r = Rig()
    # Everything hangs off a joint on the ground, so that folding him down
    # into his robe folds him towards where he stands.
    r.joint("base", (0, 0, 0))
    r.add("underglow", E(HEM_R + 0.07, (HEM_R + 0.07) * HEM_SQ, 0.003), "vex_under_glow", "base", loc=(0, 0, 0.003))
    r.joint("pelvis", (0, 0, 1.00), "base")

    # --- the robe: indigo, open down the front over violet, gold at the hem ------
    # Straight from the waist to the knee and flaring from there to the floor:
    # a bell, not a cone. A cone from the waist down was a wizard's hat.
    r.joint("skirt", (0, 0, 0.0), "pelvis")
    r.add("robe_hi", C(0.18, 0.225, 0.50, squash_y=0.80), "vex_robe", "skirt", loc=(0, 0, -0.02))
    r.add("robe", FRUSTUM(0.22, HEM_R, 0.50, squash_y=HEM_SQ), "vex_robe", "skirt", loc=(0, 0, -0.50))
    r.add("underrobe_hi", C(0.065, 0.08, 0.50, squash_y=0.35), "vex_violet", "skirt", loc=(0, -0.165, -0.02))
    flare = math.atan((HEM_R - 0.22) * HEM_SQ / 0.50)
    r.add("underrobe", C(0.08, 0.14, 0.46, squash_y=0.30), "vex_violet", "skirt",
          loc=(0, -0.22 * HEM_SQ + 0.015, -0.52), rot=(-flare, 0, 0))
    for sx in (-1, 1):
        r.limb("placket_hi", (sx * 0.07, -0.165, 0.0), (sx * 0.085, -0.185, -0.52), 0.020, "vex_gold", "skirt",
               r_tip=0.020)
        r.limb("placket", (sx * 0.085, -0.185, -0.52), (sx * 0.16, -HEM_R * HEM_SQ - 0.010, -0.95), 0.020,
               "vex_gold", "skirt", r_tip=0.022)
    scaled(r.add("hemtrim", TORUS(HEM_R - 0.012, 0.026), "vex_gold", "skirt", loc=(0, 0, -0.955)), 1.0, HEM_SQ, 1.0)
    # The hem frays into smoke: rags of it, dark violet, hung off three rings
    # that ripple against each other, long and short, some lifting away. The
    # ones in front are kept short: anything that reaches out towards the
    # camera lies along the floor and makes him look shorter, not wider.
    for name in ("hem_a", "hem_b", "hem_c"):
        r.joint(name, (0, 0, -1.0), "skirt")
    for k in range(18):
        a = math.radians(-90 + k * 20 + (8 if k % 2 else -4))
        ring = ("hem_a", "hem_b", "hem_c")[k % 3]
        long_ = (0.20, 0.08, 0.14)[k % 3] * (0.45 if math.sin(a) < -0.6 else 1.0)
        rise = (0.04, 0.0, 0.10)[(k * 7) % 3]
        x0, y0 = math.cos(a) * (HEM_R - 0.04), math.sin(a) * (HEM_R - 0.04) * HEM_SQ
        x1, y1 = math.cos(a) * (HEM_R + long_), math.sin(a) * (HEM_R + long_) * HEM_SQ
        r.limb("rag", (x0, y0, 0.12), (x1, y1, 0.0 + rise), (0.060, 0.045, 0.052)[k % 3],
               ("vex_smoke", "vex_smoke_dk", "vex_robe_dk")[(k * 2) % 3], ring, r_tip=0.010)
    # And curls of it lifting off at the sides and the back.
    for k, (x, y, z, rr) in enumerate(((-0.45, 0.06, 0.24, 0.07), (0.47, 0.02, 0.18, 0.06),
                                       (-0.32, 0.24, 0.36, 0.06), (0.28, 0.26, 0.30, 0.07),
                                       (-0.50, -0.08, 0.08, 0.05), (0.50, -0.10, 0.10, 0.05))):
        r.add("curl", E(rr, rr * 0.8, rr * 1.3), "vex_smoke", ("hem_a", "hem_b", "hem_c")[k % 3], loc=(x, y, z))

    # --- the body, the mantle, the hourglass --------------------------------------
    r.joint("chest", (0, 0, 0.02), "pelvis", rest=(2, 0, 0))
    r.add("torso", E(0.19, 0.15, 0.30), "vex_robe", "chest", loc=(0, 0, 0.22))
    # Dark under the hourglass, so the sand in it is the brightest thing there.
    r.add("breast", E(0.085, 0.05, 0.20), "vex_robe_dk", "chest", loc=(0, -0.115, 0.20))
    for sx in (-1, 1):
        r.limb("lapel", (sx * 0.06, -0.14, 0.44), (sx * 0.07, -0.16, 0.0), 0.021, "vex_gold", "chest",
               r_tip=0.021)
    # A mantle a little wider than the robe under it, so the shoulders are a
    # king's and not a monk's -- and no wider, or he is a block.
    r.add("mantle", E(0.33, 0.22, 0.11), "vex_violet", "chest", loc=(0, 0.02, 0.44))
    scaled(r.add("mantle_trim", TORUS(0.31, 0.022), "vex_gold", "chest", loc=(0, 0.01, 0.38)), 1.0, 0.70, 1.0)
    r.add("collar", E(0.14, 0.12, 0.06), "vex_robe_dk", "chest", loc=(0, 0.02, 0.52))
    # The hourglass, on a chain from the collar: gold ends and posts, and the
    # sand in it lit, waisted in the middle. Hung low enough that the mantle's
    # front edge does not cover it.
    HG = (0, -0.190, 0.15)
    for sx in (-1, 1):
        r.limb("chain", (sx * 0.10, -0.10, 0.49), (0, HG[1], HG[2] + 0.13), 0.017, "vex_gold", "chest",
               r_tip=0.015)
        r.limb("post", (sx * 0.055, HG[1] - 0.005, HG[2] + 0.11), (sx * 0.055, HG[1] - 0.005, HG[2] - 0.11),
               0.015, "vex_gold", "chest", r_tip=0.015)
    r.add("glass_top", E(0.072, 0.036, 0.024), "vex_gold", "chest", loc=(HG[0], HG[1], HG[2] + 0.120))
    r.add("glass_foot", E(0.072, 0.036, 0.024), "vex_gold", "chest", loc=(HG[0], HG[1], HG[2] - 0.120))
    # Two bulbs and a waist between them: seen from above the glass is
    # foreshortened, and bulbs any closer together run into one blob.
    r.add("sand_hi", E(0.046, 0.028, 0.036), "vex_sand_glow", "chest", loc=(HG[0], HG[1], HG[2] + 0.064))
    r.add("sand_lo", E(0.050, 0.028, 0.038), "vex_sand_glow", "chest", loc=(HG[0], HG[1], HG[2] - 0.064))
    r.add("waist", E(0.030, 0.030, 0.016), "vex_gold_dk", "chest", loc=(HG[0], HG[1] - 0.004, HG[2]))

    # --- the arms: wide sleeves, gold at the cuff, bone hands with long fingers ---
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.26, 0, 0.40), "chest", rest=(0, sx * -10, 0))
        r.limb("sleeve", (0, 0, 0.02), (0, 0, -0.27), 0.095, "vex_robe", "shoulder_" + side, r_tip=0.090)
        r.joint("elbow_" + side, (0, 0, -0.27), "shoulder_" + side, rest=(-15, 0, 0))
        r.limb("bell", (0, 0, 0), (0, 0, -0.23), 0.085, "vex_violet", "elbow_" + side, r_tip=0.125)
        r.add("cuff", TORUS(0.118, 0.024), "vex_gold", "elbow_" + side, loc=(0, 0, -0.235))
        r.joint("hand_" + side, (0, 0, -0.25), "elbow_" + side)
        r.add("palm", E(0.052, 0.030, 0.058), "vex_bone", "hand_" + side, loc=(0, 0, -0.02))
        # Long and set apart, so they are four fingers at forty pixels and not a mitten.
        for k in range(4):
            o = k - 1.5
            r.limb("finger", (o * 0.030, -0.005, -0.05), (o * 0.068, -0.025, -0.25), 0.017, "vex_bone",
                   "hand_" + side, r_tip=0.010)
        r.limb("thumb", (-sx * 0.045, -0.02, -0.03), (-sx * 0.10, -0.05, -0.13), 0.016, "vex_bone",
               "hand_" + side, r_tip=0.009)

    # --- the hood and the face in it ----------------------------------------------
    # A deep brow over the face, so the sigil has somewhere to be, cloth down
    # either side of the face to the shoulders so it is a hood and not a hat,
    # and the point rising off the top of it and bending back.
    HOOD_C, HOOD_R = Vector((0, 0.03, 0.11)), (0.17, 0.17, 0.20)
    # The point is the hood narrowing, not a cone stood on it: it starts nearly
    # as wide as the hood, which is what keeps it from being a wizard's hat.
    PT_A, PT_B, PT_RA, PT_RB = Vector((0, 0.04, 0.18)), Vector((0, 0.13, 0.46)), 0.150, 0.030
    r.joint("neck", (0, -0.02, 0.50), "chest")
    r.joint("head", (0, -0.01, 0.08), "neck", rest=(-12, 0, 0))
    r.add("hood", E(*HOOD_R), "vex_robe", "head", loc=tuple(HOOD_C))
    r.limb("hood_point", tuple(PT_A), tuple(PT_B), PT_RA, "vex_robe", "head", r_tip=PT_RB)
    r.limb("hood_tip", tuple(PT_B), (0, 0.22, 0.56), PT_RB, "vex_robe", "head", r_tip=0.008)
    for sx in (-1, 1):
        r.add("hood_side", E(0.075, 0.12, 0.17), "vex_robe", "head", loc=(sx * 0.135, 0.0, -0.05))
    r.add("hood_drape", E(0.21, 0.14, 0.11), "vex_robe_dk", "head", loc=(0, 0.06, -0.10))
    # The dark inside the hood sits behind the skull, so the skull is what shows.
    r.add("hollow", E(0.120, 0.06, 0.130), "vex_shade", "head", loc=(0, -0.075, 0.02))
    r.add("skull", E(0.088, 0.075, 0.098), "vex_bone", "head", loc=(0, -0.120, 0.015))
    r.add("cheeks", E(0.078, 0.05, 0.032), "vex_bone_dk", "head", loc=(0, -0.150, -0.030))
    for sx in (-1, 1):
        r.add("socket", E(0.036, 0.02, 0.034), "vex_void", "head", loc=(sx * 0.042, -0.180, 0.035))
        r.add("eye", E(0.025, 0.012, 0.025), "vex_eye_glow", "head", loc=(sx * 0.042, -0.194, 0.035))
    r.add("nose", E(0.014, 0.010, 0.016), "vex_void", "head", loc=(0, -0.192, -0.004))
    r.add("grin", E(0.056, 0.012, 0.014), "vex_void", "head", loc=(0, -0.180, -0.046))
    for k in range(4):
        x = (k - 1.5) * 0.026
        r.limb("tooth", (x, -0.188, -0.030), (x, -0.190, -0.062), 0.010, "vex_bone", "head", r_tip=0.008)
    r.add("rim", TORUS(0.128, 0.022), "vex_gold", "head", loc=(0, -0.135, 0.020), rot=(math.radians(84), 0, 0))

    # The sigil: a crescent moon on the brow of the hood, open to one side and
    # split across, in the same dull gold as the trim. Each bead is set on the
    # hood's own surface -- the cowl's or the point's, whichever is in front --
    # or the ones at the top float off the front of it.
    axis = PT_B - PT_A

    def inside(p):
        d = (p - HOOD_C)
        if (d.x / HOOD_R[0]) ** 2 + (d.y / HOOD_R[1]) ** 2 + (d.z / HOOD_R[2]) ** 2 <= 1.0:
            return True
        t = max(0.0, min(1.0, (p - PT_A).dot(axis) / axis.length_squared))
        return (p - (PT_A + axis * t)).length <= PT_RA + (PT_RB - PT_RA) * t

    def on_hood(x, z, lift=0.012):
        y = -0.40
        while y < 0.30 and not inside(Vector((x, y, z))):
            y += 0.002
        return y - lift

    for k in range(7):
        a = math.radians(62 + k * 39)
        if k == 3:
            continue          # the crack
        x, z = math.cos(a) * 0.066 - 0.012, 0.250 + math.sin(a) * 0.066
        r.add("moon", E(0.026, 0.018, 0.026), "vex_gold", "head", loc=(x, on_hood(x, z), z))
    x, z = -0.076, 0.238
    r.add("crack", E(0.014, 0.012, 0.020), "vex_void", "head", loc=(x, on_hood(x, z, 0.018), z))
    return r.fit("vexel", 66, VEX_REST)


# At rest the hands are held a little in front of him, the forearms forward
# and the long fingers hanging from the wrists: in front of the robe they are
# white on indigo, where hanging at his sides they were lost in its edge.
VEX_REST = {"shoulder_l": (-14, -13, 0), "shoulder_r": (-14, 13, 0), "elbow_l": X(-32), "elbow_r": X(-32),
            "hand_l": X(52), "hand_r": X(52)}


def _with_rest(**more):
    v = dict(VEX_REST)
    v.update(more)
    return v


def vex_idle(t):
    # A hover, and the hem stirring under it. The head does not move.
    s = sn(t)
    return _with_rest(_z=0.016 + 0.010 * s, chest=X(1.2 * s), skirt=(1.5 * sn(t, 0.25), 1.5 * s, 0),
                      hem_a=(3 * s, 0, 6 * s), hem_b=(0, 3 * sn(t, 0.33), -5 * sn(t, 0.33)),
                      hem_c=(-3 * sn(t, 0.66), 2 * s, 4 * sn(t, 0.66)),
                      shoulder_l=(-14 - 2 * s, -13, 0), shoulder_r=(-14 + 2 * s, 13, 0),
                      hand_l=X(52 + 6 * s), hand_r=X(52 - 6 * s))


def vex_walk(t):
    # He glides: no stride, a little bob, the robe swinging back from the way
    # he is going and side to side under him.
    s = sn(t)
    return _with_rest(_z=0.016 + 0.008 * abs(sn(t)), chest=X(3 + 1.5 * s), skirt=(-6 + 2 * sn(t * 2), 4 * s, 2 * s),
                      hem_a=(5 * s, 0, 8 * s), hem_b=(0, 5 * sn(t, 0.33), -7 * sn(t, 0.33)),
                      hem_c=(-5 * sn(t, 0.66), 3 * s, 6 * sn(t, 0.66)),
                      shoulder_l=(-14 - 4 * s, -13, 0), shoulder_r=(-14 + 4 * s, 13, 0))


# The gesture: the right hand raised high, out to the side, open, the fingers
# spread -- held -- and let fall. The rest angle's flare is added back on the
# way up: a raised arm with the hanging arm's outward lean leans in.
VEX_RAISED = {"shoulder_r": (-158, 34, 0), "elbow_r": X(-14), "hand_r": (-10, 0, 0), "%hand_r": (1.30, 1.0, 1.12),
              "chest": (-4, 0, -6), "neck": X(-6), "head": (-4, 0, -4),
              "shoulder_l": (-22, -14, 0), "elbow_l": X(-42), "hand_l": X(52), "skirt": (-2, 0, 0)}


def _raised(k, spread=None):
    """Part of the way from rest to the hand held high. mix() would blend the
    hand's scale up from nothing, so it is blended here from one."""
    v = mix(VEX_REST, {key: val for key, val in VEX_RAISED.items() if not key.startswith("%")}, k)
    sx, sy, sz = spread or VEX_RAISED["%hand_r"]
    v["%hand_r"] = (1.0 + (sx - 1.0) * k, 1.0 + (sy - 1.0) * k, 1.0 + (sz - 1.0) * k)
    return v


def vex_attack(t):
    # Up over two frames, held over three, and on the way down on the last.
    i, k = phases(t, 0.40, 0.82, 1.0)
    if i == 0:
        return _raised(k)
    if i == 1:
        # Held: the fingers open a little wider as he waits.
        w = 0.5 + 0.5 * k
        return _raised(1.0, (1.30 + 0.14 * w, 1.0, 1.12 + 0.05 * w))
    return _raised(0.40)


def vex_hurt(t):
    # Barely: the head and the shoulders turn to the viewer's right, and back.
    k = math.sin(t * math.pi)
    return _with_rest(chest=(0, 0, 10 * k), neck=(0, 0, 8 * k), head=(-2 * k, 0, 14 * k), skirt=(0, 0, 4 * k))


def vex_death(t):
    """The dissolve: he folds down into his own robe -- the hood dropping, the
    arms drawn in, everything sinking -- to a small dark knot of cloth and
    smoke on the floor. Played forwards he goes; played backwards he comes."""
    k = ease(t)
    v = _with_rest(neck=X(30 * k), head=X(34 * k), chest=X(18 * k),
                   shoulder_l=(-14 - 26 * k, -13, 30 * k), shoulder_r=(-14 - 26 * k, 13, -30 * k),
                   elbow_l=X(-32 - 60 * k), elbow_r=X(-32 - 60 * k),
                   skirt=(0, 0, 40 * k), hem_a=(0, 0, 30 * k), hem_b=(0, 0, -25 * k))
    # Height goes first and fastest; the width spreads a little as he slumps
    # and then is drawn in to the knot.
    tall = 1.0 - 0.90 * ease(t * 1.1)
    wide = 1.0 + 0.12 * math.sin(min(1.0, t * 1.4) * math.pi) - 0.45 * ease((t - 0.45) / 0.55)
    v["_squash"] = (wide, wide, max(0.08, tall))
    # The bright parts go under: the head into the hood, the hands into the sleeves.
    v["~head"] = -0.55 * ease(t * 1.3)
    v["~hand_l"] = -0.9 * ease(t * 1.6)
    v["~hand_r"] = -0.9 * ease(t * 1.6)
    v["~hem_a"] = 0.35 * k
    v["~hem_b"] = 0.30 * k
    v["~hem_c"] = 0.40 * k
    return v


# =================================================================================
#  The animated armour
# =================================================================================
def moon(r, parent, cx, y, cz, radius, bead, colour="vex_gold"):
    """The house's sigil, as beads: a crescent open to the right with one bead
    left out where it is cracked across."""
    for k in range(7):
        if k == 3:
            continue
        a = math.radians(62 + k * 39)
        r.add("moon", E(bead, bead * 0.7, bead), colour, parent,
              loc=(cx + math.cos(a) * radius, y, cz + math.sin(a) * radius))


def build_animated_armor():
    """A suit of plate with nobody in it, from the stranger's house: steel
    blued nearly black and trimmed in his dull gold, a great helm with a slit
    for a visor and only the dark behind it (the game lights the slit when it
    wakes), a short tabard in his indigo with his cracked moon on it, and a
    two-handed sword it stands leaning on -- point down between its feet, both
    gauntlets on the pommel. That is how it waits on its pedestal, and how it
    stands when it has woken."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.70))
    r.add("hips", E(0.18, 0.14, 0.11), "arm_steel_dk", "pelvis")
    humanoid_legs(r, -0.04, 0.115, 0.31, 0.31, 0.080, "arm_steel", "arm_steel_dk", foot_col="arm_steel_dk")
    for side in ("l", "r"):
        r.add("cuisse", C(0.090, 0.080, 0.19), "arm_steel", "hip_" + side, loc=(0, -0.005, -0.02))
        r.add("poleyn", E(0.074, 0.066, 0.060), "arm_steel_lt", "knee_" + side, loc=(0, -0.05, 0.0))
        r.add("greave", C(0.080, 0.070, 0.19), "arm_steel", "knee_" + side, loc=(0, -0.005, -0.04))
    r.add("fauld", C(0.20, 0.235, 0.13, squash_y=0.80), "arm_steel", "pelvis", loc=(0, 0, 0.05))
    scaled(r.add("fauld_trim", TORUS(0.228, 0.016), "vex_gold", "pelvis", loc=(0, 0, -0.08)), 1.0, 0.80, 1.0)

    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(2, 0, 0))
    r.add("cuirass", E(0.235, 0.17, 0.25), "arm_steel", "chest", loc=(0, 0, 0.25))
    r.add("breastplate", E(0.17, 0.10, 0.12), "arm_steel_lt", "chest", loc=(0, -0.085, 0.35))
    r.add("gorget", E(0.15, 0.13, 0.06), "arm_steel_dk", "chest", loc=(0, 0, 0.49))
    # Nobody in it: dark where a neck would be.
    r.add("empty", E(0.085, 0.075, 0.035), "arm_void", "chest", loc=(0, -0.01, 0.535))
    # The tabard, before and behind, and the moon on its breast.
    r.add("tabard", C(0.13, 0.15, 0.50, squash_y=0.22), "vex_robe", "chest", loc=(0, -0.175, 0.40), rot=(-0.14, 0, 0))
    r.add("tabard_hem", E(0.145, 0.035, 0.024), "vex_gold", "chest", loc=(0, -0.255, -0.21))
    r.add("tabard_back", C(0.13, 0.15, 0.50, squash_y=0.22), "vex_robe", "chest", loc=(0, 0.175, 0.40),
          rot=(0.14, 0, 0))
    # High on the breast, above where the gauntlets rest on the pommel.
    moon(r, "chest", -0.014, -0.235, 0.370, 0.064, 0.026)
    # Pauldrons on joints of their own, so a blow can knock them askew and the
    # collapse can let them slide off.
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("pauldron_" + side, (sx * 0.25, 0, 0.44), "chest")
        r.add("pauldron", E(0.135, 0.125, 0.095), "arm_steel", "pauldron_" + side, loc=(sx * 0.02, 0, 0.0))
        r.add("pauldron_rim", TORUS(0.122, 0.018), "vex_gold", "pauldron_" + side, loc=(sx * 0.02, 0, -0.045))
    humanoid_arms(r, 0.42, 0.25, 0.25, 0.24, 0.062, "arm_steel", "arm_steel_dk", flare=8)
    for side in ("l", "r"):
        r.add("couter", E(0.060, 0.060, 0.050), "arm_steel_lt", "elbow_" + side, loc=(0, 0.03, 0.0))
        r.add("gauntlet", E(0.070, 0.065, 0.075), "arm_steel_dk", "hand_" + side)
        r.add("gauntlet_cuff", TORUS(0.066, 0.016), "vex_gold", "hand_" + side, loc=(0, 0, 0.065))

    # The great helm: a flat-topped bucket, a gold band at the brow, the slit
    # under it and a gold bar down the face.
    # Taller than it is wide and flat on top: a round one read as a pot.
    r.joint("neck", (0, 0, 0.52), "chest")
    r.joint("head", (0, 0, 0.05), "neck", rest=(-4, 0, 0))
    r.add("helm", C(0.118, 0.124, 0.17), "arm_steel", "head", loc=(0, 0.0, 0.27))
    r.add("helm_top", E(0.112, 0.112, 0.030), "arm_steel_lt", "head", loc=(0, 0.0, 0.380))
    r.add("helm_band", TORUS(0.121, 0.016), "vex_gold", "head", loc=(0, 0, 0.300))
    r.add("visor", E(0.100, 0.022, 0.021), "arm_void", "head", loc=(0, -0.116, 0.255))
    r.limb("nasal", (0, -0.126, 0.232), (0, -0.124, 0.05), 0.018, "vex_gold", "head", r_tip=0.016)
    for sx in (-1, 1):
        r.add("breath", E(0.013, 0.010, 0.013), "arm_void", "head", loc=(sx * 0.058, -0.116, 0.13))
    r.add("finial", E(0.030, 0.030, 0.035), "vex_gold", "head", loc=(0, 0, 0.415))

    # The greatsword: along the hand's -Z, the grip and pommel above the fist.
    r.limb("grip", (0, 0, 0.13), (0, 0, -0.04), 0.024, "arm_grip", "hand_r", r_tip=0.024)
    r.add("pommel", E(0.042, 0.042, 0.042), "vex_gold", "hand_r", loc=(0, 0, 0.16))
    r.add("guard", E(0.20, 0.035, 0.032), "vex_gold", "hand_r", loc=(0, 0, -0.07))
    r.add("blade", C(0.055, 0.020, 0.80, squash_y=0.30), "arm_blade", "hand_r", loc=(0, 0, -0.08))
    r.add("fuller", C(0.016, 0.010, 0.60, squash_y=0.50), "arm_blade_dk", "hand_r", loc=(0, -0.012, -0.12))
    return r.fit("animated_armor", 38, ARMOR_REST)


def _grip(hold, aim, gap=0.11, **more):
    """Both hands on the hilt: the right at `hold`, the left `gap` further up
    the grip towards the pommel, the blade pointing along `aim`."""
    a = Vector(aim).normalized()
    left = Vector(hold) - a * gap
    v = {"^r": tuple(hold), "^r_aim": tuple(a), "^l": tuple(left), "^l_aim": tuple(a)}
    v.update(more)
    return v


# Waiting: the sword planted point down in front of it, both hands on the pommel.
ARMOR_REST = _grip((0.0, -0.30, 0.13), (0, 0, -1), gap=0.12)


def armor_idle(t):
    # As near to still as a sheet allows: a creak in the neck.
    v = dict(ARMOR_REST)
    v["head"] = (0, 0, 1.5 * sn(t))
    v["chest"] = X(0.6 * sn(t, 0.25))
    return v


def armor_walk(t):
    # Stiff and heavy: short steps with straight knees, the whole of it rocking
    # over each foot, and the sword shouldered.
    v = gait(t, 22, 14, 0, 0.028, 3)
    v.update(_grip((0.13, -0.17, 0.30), (0.22, 0.50, 0.84), gap=0.10))
    v.update({"pelvis": (0, 5 * sn(t), 0), "chest": (3, 0, -3 * sn(t)), "head": (0, 0, 2 * sn(t))})
    return v


# The swing, frame by frame: lifted, held high over the helm for three frames
# -- which is the warning -- and then brought down in one.
ARMOR_LIFT = _grip((0.0, -0.30, 0.44), (0, -0.25, 0.97), gap=0.10, chest=X(-4))
# Nearly upright over the helm: laid further back, the blade points at the
# camera in the row seen from behind and all but disappears.
ARMOR_HIGH = _grip((0.0, -0.04, 0.86), (0, 0.26, 0.97), gap=0.10, chest=X(-10), head=X(-6), _z=0.008)
ARMOR_CHOP = _grip((0.0, -0.32, 0.16), (0, -0.70, -0.71), gap=0.10, chest=X(24), head=X(8), _y=-0.10,
                   hip_l=fwd(26), hip_r=fwd(-14), knee_l=X(10))


def armor_attack(t):
    frame = int(round(t * 5))
    if frame == 0:
        return dict(ARMOR_REST)
    if frame == 1:
        return dict(ARMOR_LIFT)
    if frame in (2, 4):
        return dict(ARMOR_HIGH)
    if frame == 3:
        v = dict(ARMOR_HIGH)
        v.update(chest=X(-12), _z=0.014)          # straining at the top of it
        return v
    return dict(ARMOR_CHOP)


def armor_hurt(t):
    # A stagger: knocked back, the helm jolted up and turned, the pauldrons
    # thrown askew on their straps.
    k = math.sin(t * math.pi)
    v = dict(ARMOR_REST)
    v.update({"chest": X(-18 * k), "_y": 0.08 * k, "head": (12 * k, 0, 16 * k), "@head": (0, 0, 0.045 * k),
              "@pauldron_l": (-0.02 * k, 0, 0.035 * k), "@pauldron_r": (0.025 * k, 0, -0.02 * k),
              "knee_l": X(12 * k), "pauldron_l": (0, 0, 12 * k), "pauldron_r": (0, 0, -10 * k)})
    return v


def armor_death(t):
    """It comes apart: the knees go, it sinks onto its own legs, the sword falls
    away and the helm tips off and rolls clear, and what is left is a heap of
    plate with nothing in it."""
    frame = int(round(t * 5))
    keys = [
        dict(ARMOR_REST),
        _grip((0.06, -0.30, 0.08), (0.30, -0.20, -0.93), knee_l=X(30), knee_r=X(26), hip_l=fwd(18), hip_r=fwd(14),
              _z=-0.08, chest=X(10), head=(10, 0, 12)),
        dict({"^r": (0.24, -0.26, -0.05), "^r_aim": (0.75, -0.30, -0.60), "^l": (-0.22, -0.18, -0.02),
              "^l_aim": (0, 0, -1)},
             hip_l=(-50, -20, 0), hip_r=(-46, 20, 0), knee_l=X(70), knee_r=X(64), _z=-0.25, chest=X(22),
             **{"&head": (0.04, -0.02, 0.06)}, head=(20, 0, 30)),
        dict({"^r": (0.36, -0.22, -0.20), "^r_aim": (0.90, -0.30, -0.20), "^l": (-0.30, -0.16, -0.18),
              "^l_aim": (-0.4, 0, -0.9)},
             hip_l=(-78, -32, 0), hip_r=(-74, 30, 0), knee_l=X(40), knee_r=X(36), _z=-0.42, chest=X(34),
             **{"&head": (0.22, -0.12, -0.30), "&pauldron_l": (-0.03, 0, -0.03)}, head=(60, 0, 70)),
        dict({"^r": (0.40, -0.20, -0.28), "^r_aim": (0.92, -0.38, 0.05), "^l": (-0.34, -0.14, -0.26),
              "^l_aim": (-0.6, 0, -0.8)},
             hip_l=(-84, -36, 0), hip_r=(-80, 34, 0), knee_l=X(26), knee_r=X(24), _z=-0.50, chest=X(42),
             **{"&head": (0.36, -0.20, -0.70), "&pauldron_l": (-0.07, 0, -0.07), "&pauldron_r": (0.07, 0, -0.08)},
             head=(90, 0, 100)),
    ]
    if frame < len(keys):
        return keys[frame]
    v = dict(keys[-1])
    v.update({"&head": (0.38, -0.22, -0.76), "_z": -0.52, "chest": X(44)})
    return v


# =================================================================================
#  Vigil
# =================================================================================
def build_vigil():
    """The old man in the cell down the corridor, with nothing left of him but
    bone and beard: bare grey arms like sticks, hollow cheeks, the eyes sunk,
    a white beard to his chest and white hair in wisps round a bald crown, and
    rags the colour of the walls. A head big enough for a face at villager
    height. He stands at his bars holding them -- the bars are their own prop,
    drawn in front of him -- and breathes, slowly."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.50))
    r.add("hips", E(0.11, 0.085, 0.08), "vig_rag_dk", "pelvis")
    humanoid_legs(r, -0.03, 0.065, 0.23, 0.23, 0.040, "vig_skin", "vig_skin_dk", foot_col="vig_skin_dk")
    # Rags to the knee, torn at the hem.
    r.add("rags", FRUSTUM(0.12, 0.17, 0.26, squash_y=0.80), "vig_rag", "pelvis", loc=(0, 0, 0.03))
    for k in range(6):
        a = math.radians(-90 + k * 60 + 15)
        r.limb("tatter", (math.cos(a) * 0.15, math.sin(a) * 0.12, -0.18),
               (math.cos(a) * 0.18, math.sin(a) * 0.14, -0.31 - 0.04 * (k % 2)), 0.040,
               "vig_rag_dk" if k % 2 else "vig_rag", "pelvis", r_tip=0.010)
    # Stooped: bent at the chest, the neck carrying the head forward of it.
    r.joint("chest", (0, 0, 0.05), "pelvis", rest=(16, 0, 0))
    r.add("torso", E(0.12, 0.095, 0.17), "vig_rag", "chest", loc=(0, 0, 0.15))
    r.add("rag_shoulders", E(0.14, 0.10, 0.06), "vig_rag_dk", "chest", loc=(0, 0.01, 0.26))
    r.add("bare_neck", E(0.06, 0.04, 0.04), "vig_skin_dk", "chest", loc=(0, -0.07, 0.28))
    humanoid_arms(r, 0.27, 0.14, 0.17, 0.16, 0.030, "vig_skin", "vig_skin_dk", flare=8)
    for side in ("l", "r"):
        r.add("sleeve", E(0.048, 0.048, 0.055), "vig_rag", "shoulder_" + side, loc=(0, 0, -0.03))
        r.add("knuckles", E(0.034, 0.030, 0.034), "vig_skin", "hand_" + side, loc=(0, -0.01, -0.02))
    r.joint("neck", (0, -0.03, 0.30), "chest", rest=(-14, 0, 0))
    r.add("neckp", C(0.035, 0.038, 0.05), "vig_skin_dk", "neck", loc=(0, 0, 0.04))
    r.joint("head", (0, -0.01, 0.07), "neck", rest=(-8, 0, 0))
    r.add("skull", E(0.13, 0.135, 0.14), "vig_skin", "head", loc=(0, 0, 0.10))
    # White hair round the back and sides under a bald crown, thin, hanging
    # in wisps to the shoulders -- hanging, or at this size it is a pair of ears.
    r.add("fringe", E(0.138, 0.13, 0.065), "vig_hair", "head", loc=(0, 0.03, 0.065))
    r.add("hair_back", E(0.12, 0.07, 0.10), "vig_hair_dk", "head", loc=(0, 0.09, 0.03))
    for sx in (-1, 1):
        r.add("hollow", E(0.040, 0.040, 0.050), "vig_hollow", "head", loc=(sx * 0.090, -0.075, 0.045))
        r.add("socket", E(0.036, 0.018, 0.028), "vig_hollow", "head", loc=(sx * 0.050, -0.118, 0.112))
        r.add("eye", E(0.022, 0.012, 0.018), "vig_eye", "head", loc=(sx * 0.050, -0.128, 0.110))
        r.add("brow", E(0.042, 0.020, 0.018), "vig_hair", "head", loc=(sx * 0.052, -0.120, 0.150))
        for k in range(2):
            r.limb("wisp", (sx * 0.12, 0.02 + k * 0.05, 0.07 - k * 0.02),
                   (sx * (0.15 + k * 0.02), 0.05 + k * 0.06, -0.12 - k * 0.04), 0.030, "vig_hair", "head",
                   r_tip=0.008)
    r.add("nose", E(0.024, 0.035, 0.035), "vig_skin_dk", "head", loc=(0, -0.13, 0.07))
    r.add("moustache", E(0.072, 0.030, 0.024), "vig_hair", "head", loc=(0, -0.125, 0.025))
    r.add("beard", E(0.095, 0.065, 0.090), "vig_hair", "head", loc=(0, -0.090, -0.040))
    r.limb("beard_long", (0, -0.10, -0.06), (0, -0.08, -0.30), 0.075, "vig_hair", "head", r_tip=0.018)
    r.add("beard_shade", E(0.05, 0.03, 0.08), "vig_hair_dk", "head", loc=(0, -0.06, -0.20))
    return r.fit("vigil", 27, VIGIL_REST)


# At his bars: both hands up at his chest, a fist round each of two bars.
VIGIL_REST = {"^r": (0.10, -0.26, 0.20), "^r_aim": (0, -1, 0.2), "^l": (-0.10, -0.26, 0.20), "^l_aim": (0, -1, 0.2)}


def vigil_idle(t):
    # Breathing: a slow lift and settle of the chest, and the head with it.
    s = sn(t)
    v = dict(VIGIL_REST)
    v.update({"chest": X(2.5 * s), "_z": 0.006 * (s + 1), "head": X(-2 * s), "neck": X(1.5 * s)})
    return v


def vigil_walk(t):
    # A shuffle: small steps, the knees hardly bending, bent over, the arms
    # held in near the body.
    v = gait(t, 16, 10, 8, 0.010, 6)
    v.update({"shoulder_l": fwd(14 + 8 * sn(t)), "shoulder_r": fwd(14 - 8 * sn(t)), "elbow_l": X(-40),
              "elbow_r": X(-40), "head": (0, 0, 4 * sn(t))})
    return v


# The cough: the right hand up to his mouth, bent over it, and the shoulders
# jerking twice.
_MOUTH = (0.03, -0.24, 0.42)


def vigil_attack(t):
    frame = int(round(t * 5))
    held = {"^r": _MOUTH, "^r_aim": (-0.7, -0.2, 0.7), "^l": (-0.09, -0.16, 0.10), "^l_aim": (0.3, -0.5, -0.8)}
    # The body folds over the cough; the neck takes most of it back, so the
    # face -- and the fist at it -- stays turned to whoever is in front.
    poses = [
        dict(held, **{"^r": (0.07, -0.24, 0.30)}, chest=X(4)),
        dict(held, chest=X(12), neck=X(-4)),
        dict(held, chest=X(24), neck=X(-8), head=X(6), _z=-0.010, shoulder_l=(0, 0, -6)),
        dict(held, chest=X(14), neck=X(-4)),
        dict(held, chest=X(26), neck=X(-8), head=X(8), _z=-0.012),
        dict(held, **{"^r": (0.07, -0.24, 0.30)}, chest=X(8), neck=X(-2)),
    ]
    return poses[min(frame, len(poses) - 1)]


def vigil_hurt(t):
    v = dict(VIGIL_REST)
    v.update(bb.struck(math.sin(t * math.pi)))
    return v


def vigil_death(t):
    return topple(t)


# =================================================================================
#  Registration
# =================================================================================

# id: (builder, frame px, (idle, walk, attack, hurt, death), shadow radius)
CREATURES = {
    "vexel": (build_vexel, 112, (vex_idle, vex_walk, vex_attack, vex_hurt, vex_death), 0.74),
    "animated_armor": (build_animated_armor, 96,
                       (armor_idle, armor_walk, armor_attack, armor_hurt, armor_death), 0.40),
    "vigil": (build_vigil, 64, (vigil_idle, vigil_walk, vigil_attack, vigil_hurt, vigil_death), 0.26),
}


def register():
    cr.CREATURES.update(CREATURES)
