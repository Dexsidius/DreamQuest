# =============================================================================
#  blender_hexmire.py - what lives in the Hexmire: the cult that keeps the
#  drowned shrines -- its cultists, blowgunners, shamans and zealots, and the
#  High Priest they answer to -- and the Shellbacks, the old tortoise-folk of
#  the deep water, who were here first.
#
#  Rendered by tools/make_creatures.ps1 like every other monster:
#      .\tools\make_creatures.ps1 -Only hex_cultist,shellback_elder
#
#  Built out of the same parts as blender_creatures.py and fitted to a height
#  on screen the way blender_bestiary.py's monsters are (its Rig); registered
#  into the same CREATURES table, so a Shellback's sheet is made exactly the
#  way a rat's is. blender_creatures.py hands itself over at its foot.
#
#  The cult are people, and are meant to read as people first: one body for
#  all five, dressed and armed five ways. What tells them apart at thirty
#  pixels is what each one carries and wears on its head -- the cultist's
#  machete, the blowgunner's cane, the shaman's feathers and staff, the
#  zealot's mask and club, the High Priest's antlers.
#
#  The Shellbacks are tortoises that stand up. The shell is most of what makes
#  them read: it is built wider and taller than the body in front of it, so
#  from the front its rim shows round the shoulders and over the head, and
#  its plates are lighter than the seams between them so from behind it is
#  unmistakably a shell. The other thing is the claws -- three to a hand, long,
#  curved and pale against the dark skin, because they are what it fights with.
# =============================================================================

import math

from mathutils import Vector

import blender_character as bc
import blender_creatures as cr
import blender_bestiary as bb

E, C = cr.E, cr.C
X, fwd, sn, ease, phases, mix = cr.X, cr.fwd, cr.sn, cr.ease, cr.phases, cr.mix
gait, topple = cr.gait, cr.topple
humanoid_legs, humanoid_arms = cr.humanoid_legs, cr.humanoid_arms
Rig = bb.Rig
struck, swing = bb.struck, bb.swing
TORUS = bc.mesh_torus


# --- colours ---------------------------------------------------------------------------
bc.PALETTE.update({
    # The cult: skin, chalk, and the cult's red and black
    "hex_skin": (0.54, 0.37, 0.26), "hex_skin_dk": (0.40, 0.27, 0.19), "hex_chalk": (0.93, 0.91, 0.85),
    "hex_eye": (0.09, 0.07, 0.07),
    "hex_red": (0.58, 0.13, 0.13), "hex_red_dk": (0.38, 0.08, 0.09), "hex_black": (0.15, 0.13, 0.14),
    "hex_black_lt": (0.27, 0.24, 0.25),
    "hex_blade": (0.70, 0.72, 0.72), "hex_blade_dk": (0.40, 0.40, 0.43), "hex_grip": (0.36, 0.24, 0.15),
    # The blowgunner: the same cult in the swamp's own green and mud
    "hex_moss": (0.34, 0.42, 0.22), "hex_moss_dk": (0.22, 0.28, 0.15), "hex_mud": (0.42, 0.31, 0.20),
    "hex_mud_dk": (0.28, 0.20, 0.14), "hex_cane": (0.76, 0.68, 0.42), "hex_cane_dk": (0.50, 0.42, 0.24),
    "hex_puff": (0.88, 0.92, 0.80),
    # The shaman: a robe patched out of everything else, a painted mask, black
    # feathers, and the light the cult calls down
    "hex_robe": (0.38, 0.27, 0.30), "hex_robe_dk": (0.24, 0.16, 0.19), "hex_sack": (0.74, 0.64, 0.44),
    "hex_sack_dk": (0.52, 0.43, 0.29), "hex_mask": (0.82, 0.64, 0.30), "hex_mask_dk": (0.48, 0.32, 0.17),
    "hex_feather_k": (0.14, 0.13, 0.17), "hex_curse_glow": (0.98, 0.36, 0.72),
    # The zealot: red paint on bare skin, a carved mask and a club of bog-oak
    "hex_paint": (0.76, 0.14, 0.11), "hex_wood": (0.48, 0.32, 0.18), "hex_wood_dk": (0.30, 0.19, 0.11),
    # The High Priest: black and crimson, feathers with a green sheen, a dark
    # mask with green in its eyes, and the violet the whole cult is afraid of
    "hex_priest_robe": (0.14, 0.12, 0.15), "hex_crimson": (0.62, 0.09, 0.14), "hex_crimson_dk": (0.40, 0.06, 0.10),
    "hex_feather_sheen": (0.20, 0.30, 0.32), "hex_priest_mask": (0.30, 0.20, 0.15),
    "hex_priest_eye_glow": (0.52, 1.00, 0.40), "hex_priest_glow": (0.76, 0.44, 1.00),
    # The Shellbacks: olive skin, a brown-ochre shell whose plates are lighter
    # than the seams between them, a pale plastron, and paler claws
    "shell_skin": (0.52, 0.54, 0.32), "shell_skin_dk": (0.38, 0.40, 0.23), "shell_skin_lt": (0.66, 0.67, 0.42),
    "shell_seam": (0.25, 0.17, 0.10), "shell_plate": (0.58, 0.40, 0.20), "shell_plate_lt": (0.74, 0.56, 0.29),
    "shell_rim": (0.70, 0.52, 0.27), "plastron": (0.88, 0.82, 0.58), "plastron_dk": (0.60, 0.52, 0.34),
    "shell_claw": (0.93, 0.89, 0.74), "shell_beak": (0.36, 0.31, 0.21), "shell_mouth": (0.14, 0.10, 0.08),
    "shell_eye_glow": (1.00, 0.72, 0.22), "shell_sash": (0.20, 0.44, 0.52), "shell_sash_dk": (0.13, 0.30, 0.36),
    # The Snapper: a darker, rougher shell and a head made for biting
    "snap_plate": (0.42, 0.31, 0.18), "snap_plate_lt": (0.56, 0.42, 0.23), "snap_skin": (0.44, 0.47, 0.28),
    "snap_skin_dk": (0.31, 0.34, 0.19),
    # The Elder: grey with age, moss and barnacles on the shell, a pale beard
    "elder_skin": (0.56, 0.58, 0.44), "elder_skin_dk": (0.42, 0.44, 0.32), "elder_fringe": (0.86, 0.84, 0.70),
    "elder_bead": (0.30, 0.62, 0.64), "elder_plate": (0.50, 0.40, 0.26), "elder_plate_lt": (0.64, 0.54, 0.34),
})


# =================================================================================
#  The cult
# =================================================================================

def _cult_body(r, top, top_dk, wrap, headwrap, bulk=1.0, lean=8, sleeves=None, paint="hex_chalk", hem=True,
               stretch=1.0):
    """The one body under all of the cult: lean, a ragged sleeveless tunic
    over a wrap to the knee, bare shins and bare arms striped with chalk, and a
    cloth wound round the head. `bulk` broadens it for the zealot, whose arms
    are striped with `paint` instead and whose chest is bare (no `hem`);
    `stretch` lengthens legs, body and arms but not the head, for the High
    Priest, who has to stand over the rest of them."""
    st = stretch
    r.joint("pelvis", (0, 0, 0.60 * st))
    r.add("hips", E(0.14 * bulk, 0.11 * bulk, 0.10), wrap, "pelvis")
    humanoid_legs(r, -0.03, 0.085 * bulk, 0.27 * st, 0.27 * st, 0.052 * bulk, wrap, "hex_skin_dk", foot_col="hex_skin_dk")
    # The wrap: to the knee, with a ragged hem.
    r.add("wrap", C(0.15 * bulk, 0.17 * bulk, 0.14, squash_y=0.85), wrap, "pelvis", loc=(0, 0.0, 0.02))
    for k in range(5):
        r.add("tatter", E(0.05, 0.04, 0.06), top_dk if k % 2 else wrap, "pelvis",
              loc=((-0.14 + k * 0.07) * bulk, -0.10 * bulk + 0.02 * abs(k - 2), -0.26 - 0.03 * (k % 2)))
    r.joint("chest", (0, 0, 0.07), "pelvis", rest=(lean, 0, 0))
    r.add("torso", E(0.155 * bulk, 0.115 * bulk, 0.21 * st), top, "chest", loc=(0, 0, 0.19 * st))
    # A sash from the right shoulder to the left hip.
    r.add("sash", E(0.17 * bulk, 0.125 * bulk, 0.035), top_dk, "chest", loc=(0, 0, 0.18 * st), rot=(0, 0.62, 0))
    for k in range(4 if hem else 0):
        r.add("hem", E(0.05, 0.04, 0.05), top if k % 2 else top_dk, "chest",
              loc=((-0.11 + k * 0.075) * bulk, -0.09 * bulk, -0.02 - 0.02 * (k % 2)))
    humanoid_arms(r, 0.36 * st, 0.18 * bulk, 0.22 * st, 0.21 * st, 0.046 * bulk, "hex_skin", "hex_skin_dk", flare=12)
    for side in ("l", "r"):
        if sleeves:
            r.add("sleeve", E(0.07 * bulk, 0.07 * bulk, 0.09), sleeves, "shoulder_" + side, loc=(0, 0, -0.05))
        else:
            # The chalk: two bands round each upper arm and one round each
            # forearm, far enough apart that the skin shows between them --
            # closer, and at thirty pixels they were a pair of white sleeves.
            for z in (-0.05, -0.17):
                r.add("chalk", TORUS(0.046 * bulk, 0.013 * bulk), paint, "shoulder_" + side, loc=(0, 0, z))
            r.add("chalk", TORUS(0.040 * bulk, 0.013 * bulk), paint, "elbow_" + side, loc=(0, 0, -0.10))
    # A big head, its face turned up to the camera a little more than the
    # crypt's people: with the cloth on top of it the first cut showed the
    # camera a red cap and two pixels of face.
    r.joint("neck", (0, -0.02, 0.36 * st + 0.06), "chest", rest=(-12, 0, 0))
    r.joint("head", (0, -0.01, 0.06), "neck", rest=(-10, 0, 0))
    r.add("skull", E(0.104, 0.106, 0.118), "hex_skin", "head", loc=(0, 0, 0.08))
    r.add("jaw", E(0.078, 0.075, 0.05), "hex_skin", "head", loc=(0, -0.04, 0.0))
    r.add("brow", E(0.085, 0.03, 0.02), "hex_skin_dk", "head", loc=(0, -0.094, 0.115))
    for sx in (-1, 1):
        r.add("eye", E(0.024, 0.012, 0.02), "hex_eye", "head", loc=(sx * 0.042, -0.10, 0.088))
    if headwrap:
        # Wound round the crown, knotted at the back with the ends hanging:
        # set back and tipped up at the front so the brow shows under it.
        r.add("headwrap", E(0.108, 0.112, 0.06), headwrap, "head", loc=(0, 0.02, 0.17), rot=(-0.35, 0, 0))
        r.add("band", TORUS(0.10, 0.017), top_dk, "head", loc=(0, 0.015, 0.145), rot=(-0.35, 0, 0))
        r.add("knot", E(0.038, 0.032, 0.036), headwrap, "head", loc=(0, 0.115, 0.15))
        for k in (-1, 1):
            r.limb("tail", (k * 0.02, 0.12, 0.14), (k * 0.06, 0.18, 0.05), 0.022, headwrap, "head", r_tip=0.012)


def _bone_necklace(r, n=7, width=0.11, z=0.36, drop=0.08, big=None):
    """Bones on a string round the neck, the middle one hanging lowest."""
    for k in range(n):
        a = math.radians(-70 + k * 140.0 / (n - 1))
        r.add("bead", E(0.022, 0.018, 0.026), "bone_w", "chest",
              loc=(math.sin(a) * width, -0.11 + abs(math.sin(a)) * 0.04, z - math.cos(a) * drop))
    if big:
        r.add("pendant", E(big, big * 0.7, big * 1.2), "bone_w", "chest", loc=(0, -0.13, z - drop - big))


# --- Voodoo Cultist ----------------------------------------------------------------------
def build_hex_cultist():
    """The cult's rank and file: a lean man in a ragged red-and-black tunic and
    a wrap, his bare arms striped with chalk, bones on a string round his
    neck, a red cloth wound round his head and a machete as long as his arm."""
    r = Rig()
    _cult_body(r, "hex_red", "hex_black", "hex_black", "hex_red")
    _bone_necklace(r, big=0.028)
    for sx in (-1, 1):
        # Chalk down the cheeks.
        r.add("cheek", E(0.013, 0.01, 0.03), "hex_chalk", "head", loc=(sx * 0.056, -0.094, 0.045))
    # The machete: a wooden grip and a broad blade that widens to its tip,
    # carried on from the line of the forearm, turned a little so it shows its
    # face from the front as well as from the side.
    r.limb("grip", (0, 0, 0.03), (0, 0, -0.08), 0.022, "hex_grip", "hand_r", r_tip=0.022)
    r.add("guard", E(0.03, 0.05, 0.016), "hex_blade_dk", "hand_r", loc=(0, 0, -0.09))
    turn = 0.75
    c, s_ = math.cos(turn), math.sin(turn)

    def across(y):          # a point `y` across the blade's face, turned with it
        return (-y * s_, y * c)

    r.add("blade", E(0.022, 0.074, 0.20), "hex_blade", "hand_r", loc=(*across(-0.02), -0.28), rot=(0, 0, turn))
    r.add("belly", E(0.022, 0.090, 0.10), "hex_blade", "hand_r", loc=(*across(-0.03), -0.40), rot=(0, 0, turn))
    r.add("spine", E(0.024, 0.018, 0.22), "hex_blade_dk", "hand_r", loc=(*across(0.045), -0.30), rot=(0, 0, turn))
    return r.fit("hex_cultist", 30, CULT_REST)


# The machete hangs forward and down, carried on from the forearm:
# 8 (chest) - 14 (shoulder) - 15 - 30 (elbow) + 6 = -45.
CULT_REST = {"shoulder_r": fwd(14), "elbow_r": X(-30), "hand_r": X(6), "shoulder_l": fwd(10), "elbow_l": X(-24)}


def cult_idle(t):
    s = sn(t)
    v = dict(CULT_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 8 * sn(t, 0.3)),
              "shoulder_r": fwd(14 + 4 * s), "shoulder_l": fwd(10 - 4 * s)})
    return v


def cult_walk(t):
    v = gait(t, 36, 34, 0, 0.035, 8)
    v.update(CULT_REST)
    v.update({"shoulder_r": fwd(14 + 12 * sn(t)), "shoulder_l": fwd(10 - 22 * sn(t)),
              "head": (0, 0, -5 * sn(t))})
    return v


def cult_attack(t):
    # Up over the head, and down through whatever is in front of it.
    raise_ = {"shoulder_r": fwd(-160), "elbow_r": X(-30), "hand_r": X(30), "chest": (-10, 0, -16),
              "shoulder_l": fwd(40), "elbow_l": X(-30), "head": (0, 0, -10), "_y": 0.04}
    chop = {"shoulder_r": fwd(76), "elbow_r": X(-6), "hand_r": X(-6), "chest": (24, 0, 16),
            "shoulder_l": fwd(-24), "elbow_l": X(-20), "head": (0, 0, 12), "_y": -0.12,
            "hip_l": fwd(26), "hip_r": fwd(-16), "knee_r": X(16)}
    return swing(t, CULT_REST, raise_, chop, (0.38, 0.56, 1.0))


def cult_hurt(t):
    v = dict(CULT_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=fwd(-30)))
    return v


def cult_death(t):
    return topple(t, armed=True)


# --- Cultist Blowgunner ------------------------------------------------------------------
def build_hex_blowgunner():
    """The same cult in swamp green and mud-brown, a feather stood up in his
    head-cloth, a pouch of darts at his hip and a cane blowgun as tall as he
    is, feathered at the far end, carried upright like a staff until it is
    wanted."""
    r = Rig()
    _cult_body(r, "hex_moss", "hex_mud_dk", "hex_mud", "hex_moss_dk")
    _bone_necklace(r, n=5, width=0.09, drop=0.06)
    r.limb("plume", (0, 0.10, 0.19), (0.02, 0.17, 0.40), 0.026, "feather_r", "head", r_tip=0.008)
    # The pouch of darts, their flights showing out of the top of it.
    r.add("pouch", E(0.055, 0.045, 0.065), "hex_mud_dk", "pelvis", loc=(-0.16, -0.06, -0.02))
    for k in range(3):
        r.limb("dart", (-0.18 + k * 0.022, -0.06, 0.03), (-0.20 + k * 0.03, -0.07, 0.10), 0.014,
               ("feather_r", "feather_w", "feather_r")[k], "pelvis", r_tip=0.006)
    # Fitted on the man: the cane stands a head over him, and fitting the two
    # together shrank him to make room for it.
    r.fit("hex_blowgunner", 30, BLOW_REST)
    # The blowgun: a cane tube held a hand's width from the mouthpiece, ringed
    # at its joints, with feathers tied on at the far end.
    # As long as it can be: longer, and levelled at the puff it ran out of
    # its 64px frame, which slid the whole man backwards on the one frame
    # he was meant to jerk forwards.
    r.limb("tube", (0, 0, -0.10), (0, 0, 0.76), 0.030, "hex_cane", "hand_r", r_tip=0.028)
    r.add("mouthpiece", E(0.036, 0.036, 0.025), "hex_cane_dk", "hand_r", loc=(0, 0, -0.09))
    for z in (0.20, 0.40, 0.58):
        r.add("node", TORUS(0.030, 0.012), "hex_cane_dk", "hand_r", loc=(0, 0, z))
    for k, col in enumerate(("feather_r", "feather_w", "feather_b")):
        a = k * math.tau / 3
        r.limb("flight", (0, 0, 0.70), (math.cos(a) * 0.07, math.sin(a) * 0.07, 0.53), 0.024, col, "hand_r",
               r_tip=0.008)
    # The breath that sends the dart: hidden until the puff (see blow_attack).
    r.joint("puff", (0, 0, 0.76), "hand_r")
    r.add("cloud", E(0.05, 0.05, 0.05), "hex_puff", "puff")
    r.add("cloud", E(0.035, 0.035, 0.035), "hex_puff", "puff", loc=(0.04, 0.02, -0.02))
    r.j["puff"].scale = (0.01, 0.01, 0.01)
    return r


# Upright at his side and leaning back a little, the hand out past the hip:
# solved like the aim below. Leaning the other way, or held in to the body,
# the cane stood in front of him from the front and was lost against him.
BLOW_REST = {"shoulder_r": (29, -2, 0), "elbow_r": X(-50), "hand_r": (23, 4, 27), "shoulder_l": fwd(10),
             "elbow_l": X(-20), "~puff": -1.0}
# On the way up: the cane raised in front of him, pointing up and ahead. The
# attack goes through this both ways -- blended straight from the carry to the
# aim, the cane swung out sideways between them.
BLOW_LIFT = {"chest": X(4), "shoulder_r": (-31, -53, -8), "elbow_r": X(-117), "hand_r": (-29, 120, -11),
             "shoulder_l": (-123, 9, 28), "elbow_l": X(10), "~puff": -1.0}
# The mouthpiece at the lips and the cane level at whatever it is pointed at,
# the left hand out along it; solved numerically for this body, not guessed.
BLOW_AIM = {"chest": X(0), "head": X(-4), "shoulder_r": (-96, -20, -12), "elbow_r": X(-62), "hand_r": (-71, 121, 0),
            "shoulder_l": (-112, 5, 14), "elbow_l": X(0), "~puff": -1.0}
# ...and the puff: the whole of him jerks forward behind it.
BLOW_PUFF = {"chest": X(12), "head": X(-4), "shoulder_r": (-119, -28, 10), "elbow_r": X(-55), "hand_r": (-68, 141, 4),
             "shoulder_l": (-123, -1, 17), "elbow_l": X(0), "_y": -0.03, "hip_l": fwd(18), "hip_r": fwd(-12),
             "~puff": 0.0}


def blow_idle(t):
    s = sn(t)
    v = dict(BLOW_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 10 * sn(t, 0.3)), "shoulder_l": fwd(10 - 5 * s)})
    return v


def blow_walk(t):
    # The cane is carried, not swung: only the free arm goes with the stride.
    v = gait(t, 36, 34, 0, 0.035, 8)
    v.update(BLOW_REST)
    v.update({"shoulder_r": (29 - 6 * sn(t), -2, 0), "shoulder_l": fwd(10 - 24 * sn(t)), "head": (0, 0, -5 * sn(t))})
    return v


def blow_attack(t):
    # Up, to the lips, puff, and back down the way it came. Each of the six
    # frames lands on one of these, so none of them is an in-between.
    keys = (BLOW_REST, BLOW_LIFT, BLOW_AIM, BLOW_PUFF, BLOW_LIFT, BLOW_REST)
    i, k = phases(t, 0.2, 0.4, 0.6, 0.8, 1.0)
    i = min(i, 4)
    return mix(keys[i], keys[i + 1], k)


def blow_hurt(t):
    v = dict(BLOW_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=fwd(-30)))
    return v


# --- Voodoo Shaman -----------------------------------------------------------------------
def _robe(r, colour, dark, patches, r_top=0.17, r_bot=0.25, length=0.30, hem=7):
    """A robe from the waist to the ankles, ragged at the hem, with `patches`
    sewn on the front of it in whatever colours they were cut from."""
    r.add("robe", C(r_top, r_bot, length, squash_y=0.85), colour, "pelvis", loc=(0, 0.01, 0.04))
    for k in range(hem):
        a = math.radians(-80 + k * 160.0 / (hem - 1))
        r.add("tatter", E(0.06, 0.05, 0.07), dark if k % 2 else colour, "pelvis",
              loc=(math.sin(a) * r_bot * 0.9, 0.01 - math.cos(a) * r_bot * 0.78, 0.04 - length - 0.02 * (k % 2)))
    for (x, z, col) in patches:
        # On the robe's surface: its radius at that height, squashed front to back.
        rad = r_top + (r_bot - r_top) * min(1.0, max(0.0, -z / length))
        y = -math.sqrt(max(0.0, rad * rad - x * x)) * 0.85 + 0.01
        r.add("patch", E(0.065, 0.02, 0.07), col, "pelvis", loc=(x, y, 0.04 + z), rot=(0, 0, -x * 2.5))


def _plumes(r, cols, n=7, spread=150, r_in=0.07, r_out=0.24, rise=0.44, z=0.14, back=0.06, thick=0.03):
    """A fan of long feathers standing up behind the head, longest in the middle."""
    for k in range(n):
        a = math.radians(-spread / 2 + k * spread / (n - 1))
        mid = 1.0 - abs(k - (n - 1) / 2.0) / ((n - 1) / 2.0)
        r.limb("plume", (math.sin(a) * r_in, back, z + math.cos(a) * 0.02),
               (math.sin(a) * r_out, back + 0.08, z + rise * (0.62 + 0.38 * mid) * math.cos(a * 0.6)),
               thick, cols[k % len(cols)], "head", r_tip=0.008)


def build_voodoo_shaman():
    """A cult elder in a long robe patched out of everyone else's clothes: a
    painted wooden mask with the light showing in its eyes, a fan of black and
    red feathers standing up behind it, bones round the neck, and a staff with
    a horned skull on top, charms and a sacking doll hung off it, and the
    violet-red light it throws held between the horns."""
    r = Rig()
    _cult_body(r, "hex_robe", "hex_robe_dk", "hex_robe", None, lean=14, sleeves="hex_robe")
    _robe(r, "hex_robe", "hex_robe_dk",
          ((-0.08, -0.10, "hex_red"), (0.10, -0.24, "hex_moss"), (-0.03, -0.36, "hex_sack"),
           (0.08, 0.0, "hex_sack_dk"), (-0.15, -0.28, "hex_mud")))
    r.add("belt", TORUS(0.165, 0.02), "hex_sack_dk", "pelvis", loc=(0, 0.01, 0.05))
    for side in ("l", "r"):
        # Wide sleeves to the wrist.
        r.add("cuff", C(0.05, 0.075, 0.12, squash_y=0.9), "hex_robe", "elbow_" + side, loc=(0, 0, -0.02))
        r.add("cuff_rim", TORUS(0.07, 0.014), "hex_robe_dk", "elbow_" + side, loc=(0, 0, -0.20))
    _bone_necklace(r, n=7, width=0.12, z=0.37, drop=0.09, big=0.032)
    # The mask: a long face of painted wood over his own, a red stripe down it
    # and the light in the eyes.
    r.add("mask", E(0.09, 0.035, 0.13), "hex_mask", "head", loc=(0, -0.095, 0.06))
    r.add("stripe", E(0.018, 0.012, 0.11), "hex_red", "head", loc=(0, -0.128, 0.05))
    r.add("mouth", E(0.04, 0.012, 0.014), "hex_mask_dk", "head", loc=(0, -0.127, -0.02))
    for sx in (-1, 1):
        r.add("hole", E(0.026, 0.012, 0.02), "hex_mask_dk", "head", loc=(sx * 0.042, -0.124, 0.085))
        r.add("gleam", E(0.014, 0.008, 0.012), "hex_curse_glow", "head", loc=(sx * 0.042, -0.132, 0.085))
        r.add("bar", E(0.03, 0.01, 0.012), "hex_mask_dk", "head", loc=(sx * 0.055, -0.12, 0.02), rot=(0, sx * 0.5, 0))
    # A cap of feathers on the crown, and the fan standing up behind it.
    r.add("cap", E(0.105, 0.105, 0.06), "hex_feather_k", "head", loc=(0, 0.02, 0.16))
    r.add("headband", TORUS(0.10, 0.02), "hex_red", "head", loc=(0, 0.01, 0.13), rot=(-0.2, 0, 0))
    _plumes(r, ("hex_feather_k", "feather_r", "hex_feather_k", "feather_w"), n=7)
    r.fit("voodoo_shaman", 36, SHAMAN_REST)
    # The staff: driftwood, planted, a horned skull on it and the light held
    # between the horns; bones and beads hung off the skull and a sacking doll
    # with a pin through it.
    r.limb("staff", (0, 0, -0.80), (0, 0, 0.84), 0.032, "driftwood", "hand_r", r_tip=0.028)
    r.joint("staffhead", (0, 0, 0.92), "hand_r")
    r.add("skullp", E(0.09, 0.09, 0.088), "bone_w", "staffhead")
    r.add("sjaw", E(0.062, 0.058, 0.035), "bone_g", "staffhead", loc=(0, -0.03, -0.078))
    for sx in (-1, 1):
        r.add("socket", E(0.03, 0.014, 0.026), "shroud_dk", "staffhead", loc=(sx * 0.036, -0.08, 0.005))
        r.add("spark", E(0.014, 0.01, 0.012), "hex_curse_glow", "staffhead", loc=(sx * 0.036, -0.092, 0.005))
        r.limb("shorn", (sx * 0.05, 0.0, 0.04), (sx * 0.10, -0.01, 0.13), 0.026, "bone_g", "staffhead", r_tip=0.016)
        r.limb("shorn2", (sx * 0.10, -0.01, 0.13), (sx * 0.06, -0.02, 0.22), 0.016, "bone_g", "staffhead",
               r_tip=0.006)
    r.joint("light", (0, -0.01, 0.17), "staffhead")
    r.add("orb", E(0.05, 0.05, 0.05), "hex_curse_glow", "light")
    for k, (x, col) in enumerate(((-0.07, "bone_w"), (-0.03, "hex_red"))):
        r.limb("cord", (x, 0.0, -0.05), (x - 0.01, -0.01, -0.15), 0.008, "hex_sack_dk", "staffhead", r_tip=0.006)
        r.add("charm", E(0.02, 0.018, 0.03), col, "staffhead", loc=(x - 0.01, -0.01, -0.17))
    r.limb("cord", (0.06, 0.0, -0.04), (0.09, -0.01, -0.12), 0.008, "hex_sack_dk", "staffhead", r_tip=0.006)
    r.add("doll_head", E(0.03, 0.026, 0.03), "hex_sack", "staffhead", loc=(0.09, -0.01, -0.14))
    r.add("doll", E(0.035, 0.026, 0.05), "hex_sack", "staffhead", loc=(0.09, -0.01, -0.21))
    r.limb("doll_arms", (0.05, -0.01, -0.18), (0.13, -0.01, -0.18), 0.012, "hex_sack_dk", "staffhead", r_tip=0.012)
    r.limb("pin", (0.07, -0.05, -0.19), (0.11, 0.03, -0.22), 0.007, "hex_red", "staffhead", r_tip=0.006)
    return r


# The staff stands up straight, held out in front of him, solved for this
# body (see the blowgunner's aim): the chest's lean, the arm's reach and the
# hand's turn -- with its sideways part undoing the shoulder's flare -- cancel
# out. Held in at his side, the skull on it stood on his face from the side.
SHAMAN_REST = {"shoulder_r": (-58, -8, 0), "elbow_r": X(-17), "hand_r": (66, 46, 2), "shoulder_l": fwd(16),
               "elbow_l": X(-44)}
# Held up over his head, the light swelling...
SHAMAN_RAISE = {"shoulder_r": (-150, -6, 0), "elbow_r": X(-20), "hand_r": (163, -9, 39), "chest": X(-10),
                "neck": X(-10), "shoulder_l": (-110, 0, 30), "elbow_l": X(-40), "_z": 0.02, "~light": 1.0}
# ...and levelled at you.
SHAMAN_CAST = {"shoulder_r": (-76, -6, 0), "elbow_r": X(-10), "hand_r": (102, 7, 20), "chest": X(20),
               "neck": X(6), "shoulder_l": fwd(60), "elbow_l": X(-10), "_y": -0.06, "hip_l": fwd(20),
               "hip_r": fwd(-12), "~light": 0.5}


def voodoo_idle(t):
    s = sn(t)
    v = dict(SHAMAN_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 8 * sn(t, 0.3)), "shoulder_l": fwd(16 + 6 * s),
              "~light": 0.15 * sn(t * 2)})
    return v


def voodoo_walk(t):
    # A slow, deliberate walk: short steps under the robe, the staff going
    # down with each one.
    v = gait(t, 24, 24, 0, 0.025, 6)
    v.update(SHAMAN_REST)
    v.update({"shoulder_r": (-58 - 8 * sn(t), -8, 0), "shoulder_l": fwd(16 - 14 * sn(t)),
              "head": (0, 0, 6 * sn(t, 0.2)), "pelvis": (0, 4 * sn(t), 0)})
    return v


def voodoo_attack(t):
    return swing(t, SHAMAN_REST, SHAMAN_RAISE, SHAMAN_CAST, (0.42, 0.60, 1.0))


def voodoo_hurt(t):
    v = dict(SHAMAN_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=fwd(-30)))
    return v


# --- Cultist Zealot ----------------------------------------------------------------------
def build_hex_zealot():
    """The cult's heavy: a head wider across the shoulders than the others,
    bare to the waist and daubed with red, his face behind a carved wooden
    mask with bone horns and bone teeth, his hair in ropes down his back, and
    over his shoulder a two-handed club of bog-oak studded with bones."""
    r = Rig()
    _cult_body(r, "hex_skin", "hex_black", "hex_black", None, bulk=1.35, lean=10, paint="hex_paint", hem=False)
    r.add("pecs", E(0.20, 0.10, 0.09), "hex_skin", "chest", loc=(0, -0.07, 0.29))
    r.add("gut", E(0.16, 0.10, 0.10), "hex_skin_dk", "chest", loc=(0, -0.08, 0.07))
    # The red: a bar across the collarbones and a stripe down each side of
    # the chest, where it shows past the strap.
    r.add("daub", E(0.16, 0.02, 0.022), "hex_paint", "chest", loc=(0, -0.155, 0.36))
    for sx in (-1, 1):
        r.add("daub", E(0.026, 0.02, 0.10), "hex_paint", "chest", loc=(sx * 0.10, -0.15, 0.20), rot=(0, sx * 0.35, 0))
    r.add("belt", TORUS(0.20, 0.026), "hex_black_lt", "pelvis", loc=(0, 0, 0.06))
    for k in range(5):
        a = math.radians(-60 + k * 30)
        r.add("stud", E(0.022, 0.018, 0.028), "bone_w", "pelvis", loc=(math.sin(a) * 0.21, -math.cos(a) * 0.18, 0.06))
    # The mask: pale, longer than the face, horned and toothed. Carved out of
    # the same brown wood as his club it vanished into his skin.
    r.add("mask", E(0.11, 0.04, 0.145), "bone_g", "head", loc=(0, -0.10, 0.07))
    r.add("mbrow", E(0.115, 0.035, 0.028), "hex_wood_dk", "head", loc=(0, -0.13, 0.125))
    r.add("mmouth", E(0.065, 0.012, 0.022), "hex_black", "head", loc=(0, -0.14, 0.0))
    for sx in (-1, 1):
        r.add("slit", E(0.034, 0.012, 0.016), "hex_black", "head", loc=(sx * 0.046, -0.14, 0.085))
        r.add("mpaint", E(0.014, 0.012, 0.05), "hex_paint", "head", loc=(sx * 0.07, -0.135, 0.03))
        r.limb("mhorn", (sx * 0.08, -0.07, 0.15), (sx * 0.17, -0.05, 0.28), 0.034, "bone_w", "head", r_tip=0.022)
        r.limb("mhorn2", (sx * 0.17, -0.05, 0.28), (sx * 0.15, -0.01, 0.38), 0.022, "bone_w", "head", r_tip=0.006)
    for x in (-0.03, 0.0, 0.03):
        r.limb("mtooth", (x, -0.145, 0.015), (x, -0.15, -0.035), 0.012, "bone_w", "head", r_tip=0.004)
    # His hair, in ropes from the crown down the back of his neck.
    r.add("crown", E(0.10, 0.10, 0.05), "hex_black", "head", loc=(0, 0.03, 0.17))
    for k in range(5):
        x = (k - 2) * 0.045
        r.limb("rope", (x, 0.06, 0.16), (x * 1.3, 0.15, -0.10 - 0.03 * (k % 2)), 0.026, "hex_black", "head",
               r_tip=0.016)
    r.fit("hex_zealot", 36, ZEAL_REST)
    # The club: a bound haft and a head of bog-oak as thick as his thigh,
    # studded with bones and a red rag tied under it.
    r.limb("haft", (0, 0, -0.22), (0, 0, 0.30), 0.032, "hex_wood_dk", "hand_r", r_tip=0.04)
    for z in (-0.16, -0.10, 0.04):
        r.add("binding", TORUS(0.036, 0.012), "hex_black_lt", "hand_r", loc=(0, 0, z))
    r.add("club", C(0.12, 0.065, 0.32), "hex_wood", "hand_r", loc=(0, 0, 0.60))
    r.add("knot", E(0.07, 0.07, 0.05), "hex_wood_dk", "hand_r", loc=(0.05, -0.05, 0.50))
    for k in range(8):
        a = k * math.tau / 8 + 0.3
        z = 0.44 + 0.08 * (k % 3)
        rr = 0.085 + 0.04 * (z - 0.40) / 0.2
        r.limb("bonespike", (math.cos(a) * rr * 0.7, math.sin(a) * rr * 0.7, z),
               (math.cos(a) * (rr + 0.10), math.sin(a) * (rr + 0.10), z + 0.04), 0.024, "bone_w", "hand_r",
               r_tip=0.006)
    r.limb("topspike", (0, 0, 0.66), (0, 0, 0.76), 0.03, "bone_w", "hand_r", r_tip=0.006)
    r.add("rag", E(0.04, 0.03, 0.07), "hex_red", "hand_r", loc=(0.05, 0, 0.24))
    return r


# The club over his right shoulder, solved for this body (see the blowgunner).
ZEAL_REST = {"shoulder_r": (-23, 6, -26), "elbow_r": X(-117), "hand_r": (111, 22, 0), "shoulder_l": fwd(12),
             "elbow_l": X(-30)}
# Both hands on it, up and back over his head...
ZEAL_RAISE = {"chest": X(-8), "shoulder_r": (-182, -11, 16), "elbow_r": X(-18), "hand_r": (136, -2, 32),
              "shoulder_l": (-149, 32, 6), "elbow_l": X(-20), "head": X(-8), "_y": 0.04, "_z": 0.02}
# ...and brought down level through whoever is in front. It stops at the
# height of a man's chest: followed through to the ground, from the front the
# club ended a hand below his feet, in the frame under his own.
ZEAL_CHOP = {"chest": X(26), "shoulder_r": (-52, -9, -49), "elbow_r": X(-91), "hand_r": (189, -67, 21),
             "shoulder_l": (-32, 50, 96), "elbow_l": X(-85), "head": X(8), "_y": -0.08,
             "hip_l": fwd(28), "hip_r": fwd(-18), "knee_r": X(18)}


def zeal_idle(t):
    s = sn(t)
    v = dict(ZEAL_REST)
    v.update({"_z": 0.008 * s, "chest": X(3 * s), "head": (0, 0, 6 * sn(t, 0.3)), "shoulder_l": fwd(12 + 5 * s)})
    return v


def zeal_walk(t):
    # A heavy tread: the weight rolls over each foot and the free fist swings.
    v = gait(t, 32, 30, 0, 0.04, 10)
    v.update(ZEAL_REST)
    v.update({"shoulder_l": fwd(12 - 26 * sn(t)), "pelvis": (0, 6 * sn(t), 0), "head": (0, 0, -5 * sn(t))})
    return v


def zeal_attack(t):
    return swing(t, ZEAL_REST, ZEAL_RAISE, ZEAL_CHOP, (0.40, 0.58, 1.0))


def zeal_hurt(t):
    v = dict(ZEAL_REST)
    v.update(struck(math.sin(t * math.pi) * 0.8, shoulder_l=fwd(-26)))
    return v


# --- The Voodoo High Priest --------------------------------------------------------------
def build_hex_priest():
    """The one the cult answers to: a head taller than any of them, in a long
    black robe with a crimson stole and hem, a mantle of black feathers with a
    green sheen standing up behind his head, skulls on a string round his neck,
    his face a dark hooded mask with green light in its eyes and antlers out
    of it; in his right hand a staff with a great skull on it, violet light
    caged over the skull and a poppet hanging under it, and more of the same
    light cupped in his left."""
    r = Rig()
    # Long in the leg and the body, not wide: made broad, with a robe to
    # match, he read as a black egg with antlers on it.
    _cult_body(r, "hex_priest_robe", "hex_crimson", "hex_priest_robe", None, bulk=1.05, lean=4,
               sleeves="hex_priest_robe", stretch=1.28)
    _robe(r, "hex_priest_robe", "hex_crimson_dk", (), r_top=0.165, r_bot=0.24, length=0.50, hem=9)
    r.add("panel", E(0.07, 0.02, 0.34), "hex_crimson", "pelvis", loc=(0, -0.19, -0.26))
    r.add("belt", TORUS(0.17, 0.022), "hex_crimson", "pelvis", loc=(0, 0.01, 0.05))
    for side in ("l", "r"):
        r.add("cuff", C(0.055, 0.09, 0.13, squash_y=0.9), "hex_priest_robe", "elbow_" + side, loc=(0, 0, -0.02))
        r.add("cuff_rim", TORUS(0.085, 0.016), "hex_crimson", "elbow_" + side, loc=(0, 0, -0.22))
    # The mantle: black feathers over the shoulders and down the back, and a
    # collar of them standing up behind the head.
    for k in range(9):
        a = math.radians(-120 + k * 30)
        col = ("hex_priest_robe", "hex_feather_sheen", "hex_crimson_dk")[k % 3]
        r.limb("mantle", (math.sin(a) * 0.19, 0.10 - math.cos(a) * 0.10, 0.47),
               (math.sin(a) * 0.31, 0.18 - math.cos(a) * 0.10, 0.12), 0.05, col, "chest", r_tip=0.012)
    for k in range(7):
        a = math.radians(-66 + k * 22)
        col = ("hex_priest_robe", "hex_feather_sheen")[k % 2]
        r.limb("collar", (math.sin(a) * 0.12, 0.10, 0.47),
               (math.sin(a) * 0.26, 0.20, 0.77 - abs(k - 3) * 0.035), 0.036, col, "chest", r_tip=0.01)
    # Three skulls on a string, well apart: five of them close together
    # ran into one white curve under the mask, which read as a grin.
    for k in range(3):
        a = math.radians(-44 + k * 44)
        x, z = math.sin(a) * 0.13, 0.46 - math.cos(a) * 0.14
        y = -0.14 + abs(math.sin(a)) * 0.04
        r.add("nskull", E(0.04, 0.032, 0.042), "bone_w", "chest", loc=(x, y, z))
        for sx in (-1, 1):
            r.add("nsocket", E(0.011, 0.006, 0.011), "bone_cavity", "chest",
                  loc=(x + sx * 0.015, y - 0.03, z + 0.006))
        r.limb("string", (x, y + 0.01, z + 0.04), (math.sin(a) * 0.10, -0.10, 0.50), 0.008, "hex_sack_dk",
               "chest", r_tip=0.008)
    # The hood, the mask, and the antlers out of it.
    r.add("hood", E(0.13, 0.13, 0.135), "hex_priest_robe", "head", loc=(0, 0.035, 0.09))
    r.add("mask", E(0.098, 0.045, 0.16), "hex_priest_mask", "head", loc=(0, -0.10, 0.09))
    r.add("mstripe", E(0.016, 0.012, 0.13), "hex_crimson", "head", loc=(0, -0.143, 0.10))
    r.add("mmouth", E(0.045, 0.012, 0.014), "bone_cavity", "head", loc=(0, -0.14, 0.0))
    for sx in (-1, 1):
        r.add("mhole", E(0.03, 0.012, 0.022), "bone_cavity", "head", loc=(sx * 0.044, -0.138, 0.10))
        r.add("meye", E(0.026, 0.01, 0.02), "hex_priest_eye_glow", "head", loc=(sx * 0.044, -0.148, 0.10))
        r.add("mcheek", E(0.014, 0.012, 0.045), "hex_crimson", "head", loc=(sx * 0.07, -0.13, 0.035))
        r.limb("antler", (sx * 0.06, -0.06, 0.21), (sx * 0.19, -0.04, 0.38), 0.034, "bone_w", "head", r_tip=0.026)
        r.limb("antler2", (sx * 0.19, -0.04, 0.38), (sx * 0.24, -0.02, 0.60), 0.026, "bone_w", "head", r_tip=0.01)
        r.limb("tine", (sx * 0.17, -0.04, 0.34), (sx * 0.31, -0.06, 0.42), 0.02, "bone_w", "head", r_tip=0.007)
        r.limb("tine", (sx * 0.22, -0.03, 0.50), (sx * 0.33, -0.04, 0.58), 0.018, "bone_w", "head", r_tip=0.006)
        r.limb("tine", (sx * 0.21, -0.03, 0.46), (sx * 0.14, -0.05, 0.60), 0.017, "bone_w", "head", r_tip=0.006)
    # The light cupped in his left hand.
    r.joint("palm", (0, -0.07, -0.04), "hand_l")
    r.add("palm_orb", E(0.045, 0.045, 0.045), "hex_priest_glow", "palm")
    r.fit("hex_priest", 48, PRIEST_REST)
    # The staff: black wood, a great skull on it, the violet light in a cage
    # of bone above the skull and a poppet on a cord under it.
    # Gripped high, most of it below the hand: with more above it, the cast
    # levelled the skull over the top of the frame in the back view.
    r.limb("staff", (0, 0, -1.00), (0, 0, 0.76), 0.036, "hex_black_lt", "hand_r", r_tip=0.032)
    for z in (0.38, 0.46):
        r.add("sband", TORUS(0.038, 0.014), "hex_crimson", "hand_r", loc=(0, 0, z))
    r.joint("staffhead", (0, 0, 0.86), "hand_r")
    r.add("bskull", E(0.12, 0.125, 0.115), "bone_w", "staffhead")
    r.add("bjaw", E(0.08, 0.075, 0.045), "bone_g", "staffhead", loc=(0, -0.045, -0.10))
    r.add("bnose", E(0.016, 0.01, 0.02), "bone_cavity", "staffhead", loc=(0, -0.122, -0.03))
    for sx in (-1, 1):
        r.add("bsocket", E(0.036, 0.016, 0.032), "bone_cavity", "staffhead", loc=(sx * 0.046, -0.108, 0.01))
        r.add("bspark", E(0.016, 0.01, 0.014), "hex_priest_glow", "staffhead", loc=(sx * 0.046, -0.122, 0.01))
    for k in range(3):
        a = k * math.tau / 3 + 0.5
        r.limb("prong", (math.cos(a) * 0.05, math.sin(a) * 0.05, 0.09),
               (math.cos(a) * 0.10, math.sin(a) * 0.10, 0.26), 0.022, "bone_g", "staffhead", r_tip=0.008)
    r.joint("light", (0, 0, 0.19), "staffhead")
    r.add("orb", E(0.07, 0.07, 0.07), "hex_priest_glow", "light")
    r.limb("cord", (0.08, -0.02, -0.06), (0.12, -0.03, -0.18), 0.009, "hex_sack_dk", "staffhead", r_tip=0.007)
    r.add("poppet_head", E(0.042, 0.035, 0.04), "hex_sack", "staffhead", loc=(0.12, -0.03, -0.21))
    r.add("poppet", E(0.045, 0.032, 0.07), "hex_sack", "staffhead", loc=(0.12, -0.03, -0.31))
    r.limb("poppet_arms", (0.07, -0.03, -0.27), (0.17, -0.03, -0.27), 0.016, "hex_sack_dk", "staffhead",
           r_tip=0.016)
    for x0, x1 in ((0.105, 0.10), (0.135, 0.14)):
        r.limb("poppet_leg", (x0, -0.03, -0.35), (x1, -0.03, -0.42), 0.014, "hex_sack_dk", "staffhead", r_tip=0.012)
    for sx in (-1, 1):
        r.add("button", E(0.01, 0.006, 0.01), "bone_cavity", "staffhead", loc=(0.12 + sx * 0.016, -0.066, -0.205))
    r.limb("ppin", (0.10, -0.08, -0.29), (0.15, 0.02, -0.33), 0.008, "hex_crimson", "staffhead", r_tip=0.007)
    return r


# The staff upright, held out in front of him, solved for this body (see the
# blowgunner and the shaman); the left hand forward with the light in it.
PRIEST_REST = {"shoulder_r": (-54, -4, -1), "elbow_r": X(-3), "hand_r": (58, 39, -4), "shoulder_l": fwd(20),
               "elbow_l": X(-50)}
# Both arms up, the staff held high and the light swelling in it and in his
# hand...
PRIEST_RAISE = {"chest": X(-10), "shoulder_r": (-151, 6, 23), "elbow_r": X(-2), "hand_r": (166, 0, 27),
                "shoulder_l": (-150, -6, -31), "elbow_l": X(0), "head": X(-10), "_z": 0.02,
                "~light": 0.9, "~palm": 1.2}
# ...and both thrown forward at you.
PRIEST_CAST = {"chest": X(16), "shoulder_r": (-75, 6, -5), "elbow_r": X(-60), "hand_r": (161, -8, -2),
               "shoulder_l": (-76, 1, 0), "elbow_l": X(-48), "head": X(6), "_y": -0.05, "hip_l": fwd(18),
               "hip_r": fwd(-12), "~light": 0.2, "~palm": 0.6}


def priest_idle(t):
    s = sn(t)
    v = dict(PRIEST_REST)
    v.update({"_z": 0.005 * s, "chest": X(1.5 * s), "head": (0, 0, 5 * sn(t, 0.3)), "shoulder_l": fwd(20 + 4 * s),
              "~light": 0.15 * sn(t * 2), "~palm": 0.2 * sn(t * 2, 0.25)})
    return v


def priest_walk(t):
    # Unhurried: short steps under the robe, the staff going with them.
    v = gait(t, 22, 22, 0, 0.02, 4)
    v.update(PRIEST_REST)
    v.update({"shoulder_r": (-54 - 6 * sn(t), -4, -1), "shoulder_l": fwd(20 - 8 * sn(t)),
              "head": (0, 0, 4 * sn(t, 0.2)), "pelvis": (0, 3 * sn(t), 0)})
    return v


def priest_attack(t):
    return swing(t, PRIEST_REST, PRIEST_RAISE, PRIEST_CAST, (0.42, 0.60, 1.0))


def priest_hurt(t):
    v = dict(PRIEST_REST)
    v.update(struck(math.sin(t * math.pi) * 0.7, shoulder_l=fwd(-20)))
    return v


def priest_death(t):
    # To his knees and then over, the staff going down with him: the Cinder
    # King's fall, whose one joint the priest has not got (the cape) is
    # simply left out.
    return bb.king_death(t)


# =================================================================================
#  The Shellbacks
# =================================================================================

def _on_dome(centre, axes, x, z, out=0.0):
    """A point on the back of a dome (an ellipsoid at `centre` with semi-axes
    `axes`, bulging towards +Y), `x` across and `z` up from its middle, and the
    turn that lays a plate flat on it there."""
    a, b, c = axes
    k = max(0.0, 1.0 - (x / a) ** 2 - (z / c) ** 2)
    y = b * math.sqrt(k)
    n = Vector((x / (a * a), y / (b * b), z / (c * c))).normalized()
    loc = Vector((centre[0] + x, centre[1] + y, centre[2] + z)) + n * out
    return tuple(loc), tuple(Vector((0, 1, 0)).rotation_difference(n).to_euler())


def _shellback(r, p, heft=1.0, dome=1.0, head=1.25, claw=1.0, claw_r=None, lean=10, spikes=False, tilt=16):
    """The Shellback body: short, thick legs; a broad chest with a pale
    segmented plastron on it; a domed carapace on the back, wider and taller
    than the body, its rim of marginal plates showing all round it from the
    front and its back plated -- plates lighter than the seams between them,
    and paler again in their middles; a beaked head forward on a thick neck
    from under the front of the shell; and heavy arms ending in three long
    curved claws apiece.

    The shell is tipped forward over the head by `tilt`: stood straight up,
    from the front the camera saw only the dark underside of it past the
    shoulders, and the rim read as the brim of a hat.

    `p` names the colours: skin, dk, lt, seam, plate, plate_lt, rim."""
    w = heft
    r.joint("pelvis", (0, 0, 0.52))
    r.add("hips", E(0.23 * w, 0.18 * w, 0.14), p["skin"], "pelvis")
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.15 * w, 0, -0.04), "pelvis")
        r.limb("thigh", (0, 0, 0), (0, -0.01, -0.24), 0.105 * w, p["skin"], "hip_" + side, r_tip=0.092 * w)
        r.add("tscale", E(0.05, 0.035, 0.05), p["lt"], "hip_" + side, loc=(sx * 0.03, -0.085 * w, -0.10))
        r.joint("knee_" + side, (0, -0.01, -0.24), "hip_" + side)
        r.limb("shin", (0, 0, 0), (0, 0, -0.23), 0.095 * w, p["dk"], "knee_" + side, r_tip=0.09 * w)
        r.add("foot", E(0.105 * w, 0.13 * w, 0.05), p["dk"], "knee_" + side, loc=(0, -0.04, -0.245))
        for k in (-1, 0, 1):
            r.limb("nail", (k * 0.045 * w, -0.13 * w, -0.25), (k * 0.055 * w, -0.19 * w, -0.27), 0.022, "shell_claw",
                   "knee_" + side, r_tip=0.006)
    # The tail: a stub under the back of the shell.
    r.limb("tail", (0, 0.14 * w, -0.02), (0, 0.30 * w, -0.14), 0.06, p["dk"], "pelvis", r_tip=0.02)

    r.joint("chest", (0, 0, 0.06), "pelvis", rest=(lean, 0, 0))
    r.add("torso", E(0.24 * w, 0.18 * w, 0.28), p["skin"], "chest", loc=(0, 0, 0.22))
    # The plastron: pale plates either side of a seam, on a darker ground.
    r.add("plastron", E(0.20 * w, 0.07, 0.28), "plastron_dk", "chest", loc=(0, -0.13 * w, 0.21))
    for z in (0.08, 0.20, 0.32):
        narrow = 1.0 - 0.18 * abs(z - 0.20) / 0.12
        for sx in (-1, 1):
            r.add("scute", E(0.078 * w * narrow, 0.04, 0.046), "plastron", "chest",
                  loc=(sx * 0.092 * w * narrow, -0.175 * w - 0.02 * (1 - narrow), z))

    # The carapace.
    r.joint("shell", (0, 0.10 * w, 0.24), "chest", rest=(tilt, 0, 0))
    cen, ax = (0, 0.10, 0.02), (0.40 * w * dome, 0.24 * w * dome, 0.44 * dome)
    r.add("dome", E(*ax), p["seam"], "shell", loc=cen)
    # The inside of the shell, which is what shows past the shoulders from
    # the front: the plates' colour, not the seams'.
    r.add("underside", E(ax[0] * 0.97, ax[1] * 0.9, ax[2] * 0.97), p["plate"], "shell",
          loc=(cen[0], cen[1] - 0.04, cen[2]))
    # Marginal plates round the rim, alternating light and dark: the scalloped
    # edge that says "shell" from the front.
    n_rim = 16
    for k in range(n_rim):
        a = k * math.tau / n_rim
        rx, rz = math.cos(a) * ax[0] * 1.02, math.sin(a) * ax[2] * 1.02
        r.add("marginal", E(0.075 * dome, 0.06 * dome, 0.07 * dome), p["rim"] if k % 2 else p["plate"], "shell",
              loc=(rx, cen[1] - 0.01, cen[2] + rz), rot=(0, -a, 0))
    # The back: a column of three plates down the middle and three either side.
    plates = [(0, 0.26, 0.12, 0.12), (0, 0.0, 0.13, 0.12), (0, -0.26, 0.12, 0.11)]
    for sx in (-1, 1):
        plates += [(sx * 0.24, 0.20, 0.10, 0.11), (sx * 0.27, -0.03, 0.10, 0.12), (sx * 0.23, -0.26, 0.09, 0.10)]
    for (x, z, hw, hh) in plates:
        x, z, hw, hh = x * w * dome, z * dome, hw * w * dome, hh * dome
        loc, rot = _on_dome(cen, ax, x, z, out=-0.005)
        r.add("plate", E(hw, 0.035 * dome, hh), p["plate"], "shell", loc=loc, rot=rot)
        loc, rot = _on_dome(cen, ax, x, z, out=0.018 * dome)
        r.add("areola", E(hw * 0.55, 0.02 * dome, hh * 0.55), p["plate_lt"], "shell", loc=loc, rot=rot)
    if spikes:
        # A ridge down the middle, a spike to a plate, and a spike off every
        # other marginal.
        for (x, z, hw, hh) in plates[:3]:
            loc, rot = _on_dome(cen, ax, 0, z * dome, out=0.02)
            n = Vector(loc) - Vector((cen[0], cen[1], cen[2] + z * dome))
            tip = Vector(loc) + n.normalized() * 0.16 * dome + Vector((0, 0, 0.05))
            r.limb("ridge", loc, tuple(tip), 0.05 * dome, p["rim"], "shell", r_tip=0.008)
        for k in range(0, n_rim, 2):
            a = k * math.tau / n_rim
            if math.sin(a) < -0.5:
                continue        # none along the bottom edge, where the legs are
            base = (math.cos(a) * ax[0], cen[1] - 0.01, cen[2] + math.sin(a) * ax[2])
            tip = (math.cos(a) * ax[0] * 1.30, cen[1] + 0.02, cen[2] + math.sin(a) * ax[2] * 1.26)
            r.limb("rimspike", base, tip, 0.045 * dome, p["rim"], "shell", r_tip=0.008)

    # Arms: thick, scaled along the front of the forearm, three claws a hand.
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.27 * w, -0.02, 0.40), "chest", rest=(0, sx * -20, 0))
        r.add("deltoid", E(0.09 * w, 0.09 * w, 0.085), p["skin"], "shoulder_" + side)
        r.limb("upper", (0, 0, 0), (0, 0, -0.23), 0.08 * w, p["skin"], "shoulder_" + side, r_tip=0.07 * w)
        r.joint("elbow_" + side, (0, 0, -0.23), "shoulder_" + side, rest=(-15, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.21), 0.075 * w, p["dk"], "elbow_" + side, r_tip=0.07 * w)
        for k in range(3):
            r.add("fscale", E(0.04 * w, 0.03, 0.04), p["lt"], "elbow_" + side, loc=(0, -0.06 * w, -0.04 - k * 0.065))
        r.joint("hand_" + side, (0, 0, -0.21), "elbow_" + side)
        r.add("hand", E(0.085 * w, 0.075 * w, 0.075), p["dk"], "hand_" + side)
        c = claw
        # Fanned wide, so from the front they are three claws and not a
        # pale mitten; thick at the root and hooked, because long and thin
        # they read as fingers -- on the Elder, as roots.
        cr_ = claw_r or c
        for k in (-1, 0, 1):
            base = (k * 0.05 * w, -0.02, -0.05)
            mid = (k * 0.10 * w * c, -0.04 * c, -0.05 - 0.15 * c)
            tip = (k * 0.12 * w * c, -0.15 * c, -0.05 - 0.24 * c)
            r.limb("claw", base, mid, 0.042 * cr_, "shell_claw", "hand_" + side, r_tip=0.032 * cr_)
            r.limb("clawtip", mid, tip, 0.032 * cr_, "shell_claw", "hand_" + side, r_tip=0.006)

    # Neck and head: forward from under the front of the shell.
    r.joint("neck", (0, -0.10, 0.46), "chest", rest=(-24, 0, 0))
    r.limb("neckp", (0, 0.02, -0.04), (0, -0.04, 0.14), 0.085 * w, p["skin"], "neck", r_tip=0.078 * w)
    for z in (0.02, 0.08):
        r.add("wrinkle", TORUS(0.082 * w, 0.012), p["dk"], "neck", loc=(0, -0.01 - z * 0.3, z), rot=(0.3, 0, 0))
    r.joint("head", (0, -0.04, 0.14), "neck", rest=(14, 0, 0))
    h = head
    r.add("skull", E(0.12 * h, 0.14 * h, 0.105 * h), p["skin"], "head", loc=(0, -0.05 * h, 0.04 * h))
    r.add("crown", E(0.09 * h, 0.09 * h, 0.04 * h), p["lt"], "head", loc=(0, -0.03 * h, 0.12 * h))
    for sx in (-1, 1):
        r.add("brow", E(0.045 * h, 0.03 * h, 0.022 * h), p["dk"], "head", loc=(sx * 0.065 * h, -0.14 * h, 0.095 * h),
              rot=(0, sx * -0.35, 0))
        r.add("eye", E(0.026 * h, 0.018 * h, 0.024 * h), "shell_eye_glow", "head",
              loc=(sx * 0.07 * h, -0.145 * h, 0.06 * h))
    # The beak: a hooked upper bill over a lower one, the mouth line between.
    r.joint("jaw", (0, -0.06 * h, -0.02 * h), "head")
    r.add("lowbeak", E(0.075 * h, 0.10 * h, 0.035 * h), "shell_beak", "jaw", loc=(0, -0.09 * h, -0.03 * h))
    r.add("mouth", E(0.08 * h, 0.09 * h, 0.012 * h), "shell_mouth", "head", loc=(0, -0.15 * h, -0.02 * h))
    r.add("upbeak", E(0.085 * h, 0.09 * h, 0.045 * h), "shell_beak", "head", loc=(0, -0.17 * h, 0.005 * h))
    r.limb("hook", (0, -0.22 * h, 0.02 * h), (0, -0.28 * h, -0.05 * h), 0.04 * h, "shell_beak", "head",
           r_tip=0.01 * h)
    for sx in (-1, 1):
        r.add("nostril", E(0.01 * h, 0.01 * h, 0.01 * h), "shell_mouth", "head", loc=(sx * 0.025 * h, -0.235 * h,
                                                                                      0.04 * h))


SHELL = {"skin": "shell_skin", "dk": "shell_skin_dk", "lt": "shell_skin_lt", "seam": "shell_seam",
         "plate": "shell_plate", "plate_lt": "shell_plate_lt", "rim": "shell_rim"}


# --- Shellback Clawfighter ---------------------------------------------------------------
def build_shellback_clawfighter():
    """The Shellbacks' warrior: the plain build, a sash of river-blue cloth
    knotted at the hip and more of it wound round the wrists, and the claws."""
    r = Rig()
    _shellback(r, SHELL)
    r.add("sash", TORUS(0.235, 0.03), "shell_sash", "pelvis", loc=(0, 0, 0.06), rot=(0.1, 0, 0))
    r.add("knot", E(0.05, 0.04, 0.05), "shell_sash", "pelvis", loc=(0.16, -0.14, 0.04))
    r.limb("sash_end", (0.17, -0.15, 0.02), (0.20, -0.17, -0.18), 0.03, "shell_sash_dk", "pelvis", r_tip=0.02)
    for side in ("l", "r"):
        r.add("wrap", TORUS(0.078, 0.022), "shell_sash", "elbow_" + side, loc=(0, 0, -0.17))
    return r.fit("shellback_clawfighter", 34, CLAW_REST)


# Claws out and down at the sides, ready: arms forward a little and wide.
CLAW_REST = {"shoulder_l": (-14, 0, 8), "elbow_l": X(-28), "hand_l": X(-10),
             "shoulder_r": (-14, 0, -8), "elbow_r": X(-28), "hand_r": X(-10)}


def claw_idle(t):
    s = sn(t)
    v = dict(CLAW_REST)
    v.update({"_z": 0.008 * s, "chest": X(2 * s), "head": (3 * sn(t, 0.25), 0, 8 * sn(t, 0.4)),
              "shoulder_l": (-14 - 5 * s, 0, 8), "shoulder_r": (-14 + 5 * s, 0, -8)})
    return v


def claw_walk(t):
    # A waddle: short heavy steps and the whole shell rolling over them.
    v = gait(t, 28, 26, 0, 0.03, 4)
    v.update(CLAW_REST)
    v.update({"pelvis": (0, 7 * sn(t), 0), "chest": (4, 0, 5 * sn(t)), "head": (0, 0, -6 * sn(t)),
              "shoulder_l": (-14 + 16 * sn(t), 0, 8), "shoulder_r": (-14 - 16 * sn(t), 0, -8)})
    return v


# The rake: right claws up and then down across, the left on the way up behind
# them, then the left down across.
CLAW_A = {"shoulder_r": (-110, 0, -10), "elbow_r": X(-70), "hand_r": X(20), "chest": (0, 0, -16),
          "shoulder_l": (-20, 0, 8), "elbow_l": X(-40), "_y": 0.03}
CLAW_B = {"shoulder_r": (-50, 0, 20), "elbow_r": X(-6), "hand_r": X(-20), "chest": (18, 0, 16),
          "shoulder_l": (-110, 0, 10), "elbow_l": X(-70), "hand_l": X(20), "_y": -0.08, "hip_l": fwd(20),
          "hip_r": fwd(-12)}
CLAW_C = {"shoulder_l": (-50, 0, -20), "elbow_l": X(-6), "hand_l": X(-20), "chest": (20, 0, -16),
          "shoulder_r": (-30, 0, 0), "elbow_r": X(-50), "_y": -0.12, "hip_l": fwd(-12), "hip_r": fwd(22)}


def claw_attack(t):
    keys = (CLAW_REST, CLAW_A, CLAW_B, CLAW_C, CLAW_REST)
    i, k = phases(t, 0.2, 0.4, 0.6, 1.0)
    i = min(i, 3)
    return mix(keys[i], keys[i + 1], k)


def claw_hurt(t):
    v = dict(CLAW_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=(-30, 0, 20), shoulder_r=(-30, 0, -20)))
    return v


def claw_death(t):
    # Over sideways with the arms laid along the body: flung forward the way
    # topple() throws them, from the front the Elder's claws hung out of
    # the bottom of the frame.
    k = ease(t * 1.15)
    v = topple(t)
    v.update({"shoulder_l": fwd(14 * k), "shoulder_r": fwd(10 * k), "elbow_l": X(-24 * k), "elbow_r": X(-20 * k)})
    return v


# --- Shellback Snapper -------------------------------------------------------------------
SNAP = {"skin": "snap_skin", "dk": "snap_skin_dk", "lt": "shell_skin", "seam": "shell_seam",
        "plate": "snap_plate", "plate_lt": "snap_plate_lt", "rim": "shell_rim"}


def build_shellback_snapper():
    """Heavier than the warriors and darker, its shell ridged with spikes down
    the middle and round the rim, a head half as big again with a beak hooked
    like a gaff, knobbed skin on its neck and a saw-toothed tail."""
    r = Rig()
    _shellback(r, SNAP, heft=1.16, dome=1.06, head=1.6, claw=1.08, spikes=True)
    h = 1.6
    # A heavier hook on the beak, and the knobs down the neck.
    r.limb("gaff", (0, -0.24 * h, 0.03 * h), (0, -0.31 * h, -0.07 * h), 0.036 * h, "shell_beak", "head",
           r_tip=0.008 * h)
    for k in range(3):
        for sx in (-1, 1):
            r.add("knob", E(0.022, 0.022, 0.022), "shell_skin", "neck", loc=(sx * 0.07, -0.05, 0.02 + k * 0.045))
    for k in range(3):
        r.limb("tailspike", (0, 0.18 + k * 0.05, -0.04 - k * 0.04), (0, 0.22 + k * 0.05, 0.03 - k * 0.04), 0.022,
               "shell_rim", "pelvis", r_tip=0.005)
    return r.fit("shellback_snapper", 40, SNAP_REST)


# Head carried low and out, claws up in front of it.
SNAP_REST = dict(CLAW_REST, neck=X(12), head=X(-8))
# Drawn back into the shell with the beak open and the claws up...
SNAP_WIND = {"chest": X(-10), "neck": X(-26), "head": X(-6), "jaw": X(30),
             "shoulder_l": (-70, 0, 12), "elbow_l": X(-56), "hand_l": X(10),
             "shoulder_r": (-70, 0, -12), "elbow_r": X(-56), "hand_r": X(10), "_y": 0.05}
# ...and out: the whole weight forward, claws first, and the head shot out
# past them on its neck with the beak snapping shut.
SNAP_LUNGE = {"chest": X(26), "neck": X(52), "head": X(-34), "jaw": X(0),
              "shoulder_l": (-76, 0, -6), "elbow_l": X(-8), "hand_l": X(-24),
              "shoulder_r": (-76, 0, 6), "elbow_r": X(-8), "hand_r": X(-24), "_y": -0.12,
              "hip_l": fwd(30), "hip_r": fwd(-20), "knee_r": X(22)}


def snap_idle(t):
    v = claw_idle(t)
    v.update({"neck": X(12 + 3 * sn(t, 0.3)), "head": (-8, 0, 8 * sn(t, 0.4)), "jaw": X(6 * max(0.0, sn(t * 2)))})
    return v


def snap_walk(t):
    v = claw_walk(t)
    v.update({"neck": X(12), "head": (-8, 0, -6 * sn(t))})
    return v


def snap_attack(t):
    return swing(t, SNAP_REST, SNAP_WIND, SNAP_LUNGE, (0.40, 0.58, 1.0))


def snap_hurt(t):
    v = dict(SNAP_REST)
    v.update(struck(math.sin(t * math.pi), shoulder_l=(-30, 0, 20), shoulder_r=(-30, 0, -20)))
    v["neck"] = X(12 - 30 * math.sin(t * math.pi))
    return v


# --- Shellback Elder ---------------------------------------------------------------------
ELDER = {"skin": "elder_skin", "dk": "elder_skin_dk", "lt": "shell_skin_lt", "seam": "shell_seam",
         "plate": "elder_plate", "plate_lt": "elder_plate_lt", "rim": "shell_rim"}


def build_shellback_elder():
    """The oldest of them, bent under a shell grown twice the size of a
    warrior's and gone green with moss, barnacles along its rim and weed
    hanging off the back of it; grey in the skin, a long pale fringe of it
    hanging off the jaw like a beard, a string of shells and blue beads round
    the neck, and claws the length of a man's forearm."""
    r = Rig()
    # Stooped, but not so far that the shell becomes a roof over him: bent
    # any further, from the front he was a mushroom with claws under it.
    _shellback(r, ELDER, heft=1.28, dome=1.16, head=1.4, claw=1.22, claw_r=1.55, lean=18, tilt=10)
    cen, ax = (0, 0.10, 0.02), (0.40 * 1.28 * 1.16, 0.24 * 1.28 * 1.16, 0.44 * 1.16)
    # Moss on the upper back of the shell, in cushions.
    for (x, z, rr, col) in ((-0.12, 0.34, 0.10, "moss"), (0.16, 0.28, 0.09, "moss_lt"), (0.0, 0.46, 0.08, "moss_lt"),
                            (-0.30, 0.12, 0.08, "moss"), (0.32, 0.06, 0.07, "moss"), (0.08, 0.14, 0.07, "moss")):
        loc, rot = _on_dome(cen, ax, x, z, out=0.015)
        r.add("moss", E(rr, rr * 0.45, rr * 0.85), col, "shell", loc=loc, rot=rot)
    # Barnacles along the rim.
    for k in range(9):
        a = math.radians(10 + k * 20)
        r.add("barnacle", E(0.035, 0.035, 0.03), "barnacle", "shell",
              loc=(math.cos(a) * ax[0] * 1.06, cen[1] + 0.01, cen[2] + math.sin(a) * ax[2] * 1.06))
    # Weed hanging off the back.
    for k, x in enumerate((-0.34, -0.16, 0.02, 0.20, 0.36)):
        loc, _ = _on_dome(cen, ax, x, -0.20, out=0.0)
        r.limb("weed", loc, (loc[0] * 1.05, loc[1] + 0.02, loc[2] - 0.30 - 0.06 * (k % 2)), 0.028, "weed", "shell",
               r_tip=0.012)
    # The beard: a fringe of pale skin off the jaw and throat.
    h = 1.4
    for k in range(5):
        x = (k - 2) * 0.035 * h
        r.limb("fringe", (x, -0.10 * h, -0.05 * h), (x * 1.3, -0.08 * h, -0.20 * h - 0.03 * (k % 2)), 0.024 * h,
               "elder_fringe", "jaw", r_tip=0.01)
    # Shells and beads on a string.
    for k in range(7):
        a = math.radians(-60 + k * 20)
        r.add("bead", E(0.03, 0.025, 0.03), "barnacle" if k % 2 else "elder_bead", "chest",
              loc=(math.sin(a) * 0.20, -0.20 + abs(math.sin(a)) * 0.05, 0.44 - math.cos(a) * 0.10))
    return r.fit("shellback_elder", 50, ELDER_REST)


ELDER_REST = {"shoulder_l": (-18, 0, 10), "elbow_l": X(-30), "hand_l": X(-6),
              "shoulder_r": (-18, 0, -10), "elbow_r": X(-30), "hand_r": X(-6), "neck": X(-8), "head": X(4)}
# Up on its hind legs, the claws over its head...
ELDER_REAR = {"chest": X(-34), "neck": X(-16), "head": X(-4), "jaw": X(24),
              "shoulder_l": (-150, 0, 16), "elbow_l": X(-34), "hand_l": X(10),
              "shoulder_r": (-150, 0, -16), "elbow_r": X(-34), "hand_r": X(10), "_z": 0.04, "_y": 0.06}
# ...and all of it comes down.
ELDER_SLAM = {"chest": X(30), "neck": X(8), "head": X(-6), "jaw": X(12),
              "shoulder_l": (-44, 0, 4), "elbow_l": X(-6), "hand_l": X(-16),
              "shoulder_r": (-44, 0, -4), "elbow_r": X(-6), "hand_r": X(-16), "_z": -0.04, "_y": -0.10,
              "hip_l": fwd(24), "knee_r": X(22)}


def elder_idle(t):
    s = sn(t)
    v = dict(ELDER_REST)
    v.update({"_z": 0.008 * s, "chest": X(2 * s), "neck": X(-8 + 3 * sn(t, 0.3)), "head": (4, 0, 6 * sn(t, 0.4)),
              "shoulder_l": (-18 - 4 * s, 0, 10), "shoulder_r": (-18 + 4 * s, 0, -10), "jaw": X(4 * max(0.0, s))})
    return v


def elder_walk(t):
    # Slow, and all of it rolling side to side over each step.
    v = gait(t, 22, 22, 0, 0.03, 4)
    v.update(ELDER_REST)
    v.update({"pelvis": (0, 8 * sn(t), 0), "chest": (4, 0, 6 * sn(t)), "head": (4, 0, -6 * sn(t)),
              "shoulder_l": (-18 + 12 * sn(t), 0, 10), "shoulder_r": (-18 - 12 * sn(t), 0, -10)})
    return v


def elder_attack(t):
    return swing(t, ELDER_REST, ELDER_REAR, ELDER_SLAM, (0.44, 0.62, 1.0))


def elder_death(t):
    # The same fall, lying a little higher: a shell that size, on its side,
    # otherwise reached past the bottom of the frame.
    v = claw_death(t)
    v["_wz"] = v.get("_wz", 0.0) + 0.10 * ease(t * 1.15)
    return v


def elder_hurt(t):
    v = dict(ELDER_REST)
    v.update(struck(math.sin(t * math.pi) * 0.7, shoulder_l=(-34, 0, 20), shoulder_r=(-34, 0, -20)))
    return v


# =================================================================================
#  Registration
# =================================================================================

# id: (builder, frame px, (idle, walk, attack, hurt, death), shadow radius)
CREATURES = {
    "hex_cultist":    (build_hex_cultist, 64, (cult_idle, cult_walk, cult_attack, cult_hurt, cult_death), 0.30),
    "hex_blowgunner": (build_hex_blowgunner, 64, (blow_idle, blow_walk, blow_attack, blow_hurt, cult_death), 0.30),
    "voodoo_shaman":  (build_voodoo_shaman, 96,
                       (voodoo_idle, voodoo_walk, voodoo_attack, voodoo_hurt, cult_death), 0.30),
    "hex_zealot":     (build_hex_zealot, 96, (zeal_idle, zeal_walk, zeal_attack, zeal_hurt, cult_death), 0.36),
    "hex_priest":     (build_hex_priest, 112,
                       (priest_idle, priest_walk, priest_attack, priest_hurt, priest_death), 0.44),
    "shellback_clawfighter": (build_shellback_clawfighter, 72,
                              (claw_idle, claw_walk, claw_attack, claw_hurt, claw_death), 0.44),
    "shellback_snapper":     (build_shellback_snapper, 80,
                              (snap_idle, snap_walk, snap_attack, snap_hurt, claw_death), 0.52),
    "shellback_elder":       (build_shellback_elder, 96,
                              (elder_idle, elder_walk, elder_attack, elder_hurt, elder_death), 0.60),
}


def register():
    cr.CREATURES.update(CREATURES)
