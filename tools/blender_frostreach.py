# =============================================================================
#  blender_frostreach.py - what walks the Frostreach, the frozen country off
#  the Ice Spire: the draugr -- the barrow-dead, swordsmen and bowmen -- and
#  the Warlords they rise for; the Frostback Troll, the Ice Troll's older and
#  bigger cousin; and the Abominable Snowman, which is rarely seen and never
#  twice by the same man.
#
#  Rendered by tools/make_creatures.ps1 like every other monster:
#      .\tools\make_creatures.ps1 -Only draugr,abominable_snowman
#
#  Built out of the same parts as blender_creatures.py and fitted to a height
#  on screen the way blender_bestiary.py's monsters are (its Rig); registered
#  into the same CREATURES table, so a draugr's sheet is made exactly the way a
#  rat's is. blender_creatures.py hands itself over at its foot.
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
    # The draugr: barrow-dead skin gone blue-grey, rusted mail, wool dyed red
    # a long time ago, fur, old iron, and rime over all of it
    "draugr_skin": (0.53, 0.59, 0.63), "draugr_skin_dk": (0.38, 0.43, 0.48), "draugr_socket": (0.11, 0.12, 0.16),
    "draugr_eye_glow": (0.56, 0.93, 1.00),
    "draugr_mail": (0.52, 0.54, 0.57), "draugr_mail_dk": (0.33, 0.34, 0.38), "draugr_rust": (0.52, 0.34, 0.21),
    "draugr_wool": (0.52, 0.22, 0.18), "draugr_wool_dk": (0.35, 0.16, 0.14),
    "draugr_fur": (0.52, 0.45, 0.37), "draugr_fur_dk": (0.35, 0.30, 0.25),
    "draugr_leather": (0.33, 0.24, 0.17), "draugr_wrap": (0.62, 0.57, 0.47),
    "draugr_iron": (0.32, 0.33, 0.37), "draugr_iron_lt": (0.54, 0.56, 0.60), "draugr_rime": (0.88, 0.94, 0.98),
    "draugr_beard": (0.87, 0.87, 0.83), "draugr_beard_dk": (0.64, 0.65, 0.64),
    "draugr_wood": (0.50, 0.36, 0.22), "draugr_wood_dk": (0.34, 0.24, 0.15),
    "draugr_blade": (0.66, 0.68, 0.70), "draugr_blade_dk": (0.40, 0.41, 0.44),
    # The bowman: a wolf-grey fur cloak, and a yew bow pale enough to show on it
    "draugr_cloak": (0.42, 0.40, 0.38), "draugr_cloak_dk": (0.28, 0.27, 0.27),
    "draugr_bow": (0.66, 0.47, 0.26), "draugr_string": (0.86, 0.84, 0.76), "draugr_fletch": (0.90, 0.88, 0.84),
    # The Warlord: blackened plate with frost in every hollow of it, a wolf
    # pelt, a cloak that was crimson, a crown of dull gold and antlers
    "wl_iron": (0.24, 0.24, 0.28), "wl_iron_dk": (0.14, 0.14, 0.17), "wl_iron_lt": (0.42, 0.44, 0.49),
    "wl_rime": (0.86, 0.93, 0.98), "wl_fur": (0.78, 0.78, 0.76), "wl_fur_dk": (0.55, 0.56, 0.57),
    "wl_crimson": (0.48, 0.11, 0.13), "wl_crimson_dk": (0.31, 0.07, 0.09), "wl_gold": (0.68, 0.55, 0.28),
    "wl_antler": (0.80, 0.76, 0.66), "wl_antler_dk": (0.58, 0.54, 0.46), "wl_haft": (0.32, 0.23, 0.15),
    "wl_blade": (0.32, 0.33, 0.37), "wl_edge": (0.80, 0.86, 0.92),
    # The Frostback: the Ice Troll's pale blue hide, shaggy white fur with blue
    # in it, and ice grown into it -- a deeper, clearer blue than anything
    # else on it, lit at the tips
    "fb_skin": (0.60, 0.72, 0.84), "fb_skin_dk": (0.42, 0.53, 0.67), "fb_skin_lt": (0.74, 0.84, 0.92),
    "fb_fur": (0.91, 0.94, 0.97), "fb_fur_dk": (0.70, 0.78, 0.88),
    "fb_crystal": (0.34, 0.64, 0.94), "fb_crystal_dk": (0.20, 0.42, 0.74), "fb_crystal_glow": (0.70, 0.96, 1.00),
    "fb_eye_glow": (0.45, 0.95, 1.00), "fb_tusk": (0.94, 0.91, 0.80), "fb_mouth": (0.18, 0.20, 0.30),
    "fb_hide": (0.47, 0.43, 0.39), "fb_hide_dk": (0.32, 0.29, 0.26), "fb_nail": (0.26, 0.28, 0.35),
    # The Snowman: white fur gone blue in its shadows, a leathery blue-grey
    # face and palms, pale claws, and small eyes like coals
    "yeti_fur": (0.95, 0.96, 0.98), "yeti_fur_md": (0.84, 0.88, 0.94), "yeti_fur_dk": (0.66, 0.74, 0.86),
    "yeti_face": (0.60, 0.66, 0.74), "yeti_face_dk": (0.42, 0.47, 0.56), "yeti_palm": (0.30, 0.33, 0.41),
    "yeti_claw": (0.88, 0.86, 0.78), "yeti_mouth": (0.22, 0.10, 0.14), "yeti_fang": (0.96, 0.95, 0.90),
    "yeti_eye_glow": (1.00, 0.46, 0.22),
})


# =================================================================================
#  The draugr
# =================================================================================

def _band(r, parent, rx, ry, z, colour, cy=0.0, t=0.012):
    """A thin ring round an ellipsoid's waist, a hair proud of it: a row of
    mail, a seam, a strap."""
    r.add("band", E(rx + 0.006, ry + 0.006, t), colour, parent, loc=(0, cy, z))


def _ring(rx, ry, cz, rz, z):
    """How wide an ellipsoid (semi-axes rx, ry, rz, centred at height cz) is
    at height z."""
    k = max(0.0, 1.0 - ((z - cz) / rz) ** 2) ** 0.5
    return rx * k, ry * k


def _draugr_body(r, mantle=True):
    """The one body under both draugr: a tall gaunt dead man in a byrnie of
    rusted ring-mail to the thigh over a red wool tunic, leg-wraps to the
    knee, a fur mantle on the shoulders, bare blue-grey arms with leather at
    the wrists, and a long white beard in two braids."""
    # Long in the leg and narrow in the waist: built to the cult's
    # proportions he was a barrel in a helmet.
    r.joint("pelvis", (0, 0, 0.64))
    r.add("hips", E(0.13, 0.10, 0.10), "draugr_wool_dk", "pelvis")
    humanoid_legs(r, -0.03, 0.08, 0.29, 0.29, 0.047, "draugr_wool_dk", "draugr_leather", foot_col="draugr_fur_dk")
    for side in ("l", "r"):
        # Leg-wraps, wound up the shin.
        for k in range(3):
            r.add("wrap", TORUS(0.041, 0.011), "draugr_wrap", "knee_" + side, loc=(0, 0, -0.05 - k * 0.075),
                  rot=(0.35 * (1 - 2 * (k % 2)), 0, 0))
    # The tunic's hem shows under the mail, torn.
    r.add("tunic", C(0.13, 0.16, 0.19, squash_y=0.85), "draugr_wool", "pelvis", loc=(0, 0, 0.03))
    for k in range(6):
        a = math.radians(-75 + k * 30)
        r.add("tatter", E(0.045, 0.035, 0.06), "draugr_wool_dk" if k % 2 else "draugr_wool", "pelvis",
              loc=(math.sin(a) * 0.14, -math.cos(a) * 0.115, -0.20 - 0.03 * (k % 2)))
    # The byrnie's skirt.
    r.add("mailskirt", C(0.14, 0.16, 0.10, squash_y=0.86), "draugr_mail", "pelvis", loc=(0, 0, 0.05))
    _band(r, "pelvis", 0.155, 0.135, -0.03, "draugr_mail_dk")
    r.add("belt", TORUS(0.135, 0.02), "draugr_leather", "pelvis", loc=(0, 0, 0.075))
    r.add("buckle", E(0.03, 0.02, 0.026), "draugr_iron_lt", "pelvis", loc=(0, -0.13, 0.075))

    r.joint("chest", (0, 0, 0.07), "pelvis", rest=(8, 0, 0))
    # A narrow waist under a broad chest.
    r.add("torso", E(0.13, 0.10, 0.21), "draugr_mail", "chest", loc=(0, 0, 0.18))
    r.add("chestp", E(0.165, 0.115, 0.12), "draugr_mail", "chest", loc=(0, 0, 0.29))
    # Rows of rings: darker lines round the byrnie, which is all mail is at
    # thirty pixels.
    for z in (0.08, 0.16):
        rx, ry = _ring(0.13, 0.10, 0.18, 0.21, z)
        _band(r, "chest", rx, ry, z, "draugr_mail_dk")
    for z in (0.25, 0.32):
        rx, ry = _ring(0.165, 0.115, 0.29, 0.12, z)
        _band(r, "chest", rx, ry, z, "draugr_mail_dk")
    for (x, z, s) in ((-0.07, 0.22, 0.03), (0.08, 0.10, 0.025)):
        r.add("rust", E(s, 0.02, s * 0.8), "draugr_rust", "chest", loc=(x, -0.10, z))
    humanoid_arms(r, 0.36, 0.18, 0.22, 0.21, 0.04, "draugr_skin", "draugr_skin", flare=12)
    for side in ("l", "r"):
        # Mail sleeves to the elbow, bare dead arm under them, a leather cuff.
        r.add("sleeve", C(0.058, 0.052, 0.11), "draugr_mail", "shoulder_" + side, loc=(0, 0, -0.01))
        _band(r, "shoulder_" + side, 0.05, 0.05, -0.10, "draugr_mail_dk")
        r.add("cuff", C(0.052, 0.05, 0.06), "draugr_leather", "elbow_" + side, loc=(0, 0, -0.12))
        r.add("fist", E(0.048, 0.046, 0.05), "draugr_skin_dk", "hand_" + side, loc=(0, -0.01, -0.01))
    if mantle:
        # A fur collar on the shoulders and a short fur cape down the back.
        # Longer, and brown all round, it swallowed the mail: from every side
        # the dead man was a sack of fur.
        for k in range(7):
            a = math.radians(-90 + k * 30)
            r.limb("mantle", (math.sin(a) * 0.13, 0.04 + math.cos(a) * 0.06, 0.44),
                   (math.sin(a) * 0.20, 0.06 + math.cos(a) * 0.09, 0.37), 0.042,
                   "draugr_fur" if k % 2 else "draugr_fur_dk", "chest", r_tip=0.02)
        for k in range(3):
            r.add("cape", E(0.06, 0.025, 0.13), "draugr_fur_dk" if k % 2 else "draugr_fur", "chest",
                  loc=(-0.09 + k * 0.09, 0.135, 0.22 - (k % 2) * 0.05))

    r.joint("neck", (0, -0.02, 0.42), "chest", rest=(-12, 0, 0))
    r.joint("head", (0, -0.01, 0.06), "neck", rest=(-10, 0, 0))
    # A dead man's face: skin pulled tight over the skull, the cheeks fallen
    # in, dark sockets and the cold light in them.
    r.add("skull", E(0.098, 0.102, 0.118), "draugr_skin", "head", loc=(0, 0, 0.08))
    r.add("jaw", E(0.07, 0.07, 0.045), "draugr_skin", "head", loc=(0, -0.035, 0.0))
    for sx in (-1, 1):
        r.add("hollow", E(0.02, 0.012, 0.03), "draugr_skin_dk", "head", loc=(sx * 0.052, -0.086, 0.04))
        r.add("socket", E(0.033, 0.012, 0.027), "draugr_socket", "head", loc=(sx * 0.042, -0.094, 0.09))
        r.add("eye", E(0.025, 0.012, 0.019), "draugr_eye_glow", "head", loc=(sx * 0.042, -0.104, 0.09))
    # The beard: long, white, in two braids with iron rings on the ends, and a
    # moustache hanging into it.
    r.add("beard", E(0.082, 0.05, 0.08), "draugr_beard", "head", loc=(0, -0.07, -0.035))
    for sx in (-1, 1):
        r.limb("moustache", (sx * 0.012, -0.108, 0.045), (sx * 0.05, -0.10, -0.02), 0.017, "draugr_beard", "head",
               r_tip=0.012)
        r.limb("braid", (sx * 0.032, -0.08, -0.08), (sx * 0.03, -0.075, -0.18), 0.03, "draugr_beard", "head",
               r_tip=0.025)
        r.limb("braid2", (sx * 0.03, -0.075, -0.18), (sx * 0.026, -0.07, -0.28), 0.025, "draugr_beard_dk", "head",
               r_tip=0.018)
        r.add("bead", E(0.022, 0.022, 0.02), "draugr_iron_lt", "head", loc=(sx * 0.026, -0.07, -0.30))
    # A head a size up from the cult's: between the helm's rim and the beard
    # there were two pixels of face, and the light in the eyes fell between
    # them.
    r.j["head"].scale = (1.14, 1.14, 1.14)


def _nasal_helm(r):
    """A spangenhelm: an iron cap drawn up to a point, a ridge up the front
    of it and a nasal down between the eyes."""
    r.add("helm", E(0.112, 0.118, 0.075), "draugr_iron", "head", loc=(0, 0.008, 0.15))
    r.add("rim", E(0.116, 0.122, 0.02), "draugr_iron_lt", "head", loc=(0, 0.004, 0.128))
    r.limb("cone", (0, 0.012, 0.18), (0, 0.025, 0.35), 0.08, "draugr_iron", "head", r_tip=0.01)
    r.limb("ridge", (0, -0.10, 0.14), (0, -0.01, 0.32), 0.015, "draugr_iron_lt", "head", r_tip=0.01)
    r.add("nasal", E(0.017, 0.014, 0.05), "draugr_iron_lt", "head", loc=(0, -0.119, 0.098))
    # No rime on it: a cap of it on the crown read from above as a white
    # crescent moon, a ball of it on the point as the bobble on a woolly hat.


def _sword(r, parent="hand_r"):
    """A broad old sword, notched, carried on from the line of the forearm the
    way the cult's machetes are: a raised wrist stands it up, a straight one
    points it where the arm goes. Its flat faces the way the knuckles do."""
    r.limb("grip", (0, 0, 0.04), (0, 0, -0.04), 0.018, "draugr_leather", parent, r_tip=0.018)
    r.add("pommel", E(0.032, 0.026, 0.022), "draugr_iron_lt", parent, loc=(0, 0, 0.055))
    r.add("guard", E(0.075, 0.022, 0.017), "draugr_iron", parent, loc=(0, 0, -0.05))
    r.add("blade", E(0.048, 0.014, 0.25), "draugr_blade", parent, loc=(0, 0, -0.30))
    r.add("fuller", E(0.012, 0.016, 0.20), "draugr_blade_dk", parent, loc=(0, 0, -0.28))
    for (x, z) in ((0.05, -0.22), (-0.046, -0.36)):
        r.add("notch", E(0.018, 0.017, 0.02), "draugr_blade_dk", parent, loc=(x, 0, z))


def _round_shield(r, parent="elbow_l", at=0.21, radius=0.20):
    """A round shield of planks, iron-rimmed and bossed, held by the grip
    behind the boss: so it faces along the forearm, and wherever the forearm
    points, the shield's face does. Rime on its upper edge."""
    z = -at - 0.04
    # Turned out a little from the forearm, so from the side it shows its face
    # and not only its edge.
    turn = (0, 0.30, 0)
    r.add("shield", E(radius, radius, 0.035), "draugr_wood", parent, loc=(0, 0, z), rot=turn)
    rim = r.add("shield_rim", TORUS(radius - 0.005, 0.02), "draugr_iron", parent, loc=(0, 0, z), rot=turn)
    for k in (-1, 1):
        r.add("plank", E(radius * 0.92, 0.008, 0.037), "draugr_wood_dk", parent,
              loc=(0, k * radius * 0.36, z), rot=turn)
    r.add("boss", E(0.058, 0.058, 0.05), "draugr_iron_lt", parent,
          loc=(-math.sin(turn[1]) * 0.04, 0, z - math.cos(turn[1]) * 0.04), rot=turn)
    for k in range(4):
        a = math.radians(-60 + k * 40)
        # Up the shield is its -Y once the forearm is raised level.
        r.add("rime", E(0.05, 0.03, 0.04), "draugr_rime", parent,
              loc=(math.sin(a) * radius * 0.85 * math.cos(turn[1]), -math.cos(a) * radius * 0.85,
                   z - math.sin(a) * radius * 0.85 * math.sin(turn[1])), rot=turn)
    return rim


# --- Draugr ---------------------------------------------------------------------------
def build_draugr():
    """A barrow-dead swordsman: the byrnie and the braids, a spangenhelm with
    rime on it, a round shield held out in front and a notched sword held up
    beside the helm."""
    r = Rig()
    _draugr_body(r)
    _nasal_helm(r)
    _sword(r)
    _round_shield(r)
    return r.fit("draugr", 32, DRAUGR_REST)


# Solved for this body (scratchpad fk search, as the Hexmire's were): the
# shield held out before the left shoulder, its face to you and a little out;
# the sword up and out past the right shoulder, the wrist cocked back to stand
# it upright. Held in front of him the blade stood on the grey of his mail and
# vanished.
DRAUGR_REST = {"shoulder_l": (-9, -18, -12), "elbow_l": X(-73),
               "shoulder_r": (3, 10, 36), "elbow_r": X(-80), "hand_r": (-84, 36, 19)}
# The shield shoved out and up, the sword raised over the head and laid back...
DRAUGR_RAISE = {"chest": (-8, 0, -18), "head": (0, 0, 10), "_y": 0.03,
                "shoulder_r": (-155, 0, 15), "elbow_r": X(-40), "hand_r": X(-30),
                "shoulder_l": (-40, -18, -12), "elbow_l": X(-55)}
# ...and brought down past the shield's edge, the whole of him behind it.
DRAUGR_CHOP = {"chest": (22, 0, 16), "head": (0, 0, 8), "_y": -0.10,
               "shoulder_r": (-70, 0, -12), "elbow_r": X(-6), "hand_r": X(-6),
               "shoulder_l": (-10, -18, -30), "elbow_l": X(-85),
               "hip_l": fwd(26), "hip_r": fwd(-16), "knee_r": X(16)}


def draugr_idle(t):
    s = sn(t)
    v = dict(DRAUGR_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 6 * sn(t, 0.3)),
              "shoulder_r": (3 - 4 * s, 10, 36), "shoulder_l": (-9 + 3 * s, -18, -12)})
    return v


def draugr_walk(t):
    # A stiff, heavy tread: the dead do not swing their arms, they carry them.
    v = gait(t, 34, 32, 0, 0.03, 8)
    v.update(DRAUGR_REST)
    v.update({"shoulder_r": (3 + 10 * sn(t), 10, 36), "shoulder_l": (-9 - 8 * sn(t), -18, -12),
              "head": (0, 0, -5 * sn(t)), "pelvis": (0, 4 * sn(t), 0)})
    return v


def draugr_attack(t):
    return swing(t, DRAUGR_REST, DRAUGR_RAISE, DRAUGR_CHOP, (0.38, 0.56, 1.0))


def draugr_hurt(t):
    v = dict(DRAUGR_REST)
    v.update(struck(math.sin(t * math.pi)))
    return v


def _settle(rest, fall, t, over=0.4):
    """A fall that starts from the creature's own stance, not from its bare
    rig: topple() is written from a zero pose, and cut straight to from a
    raised sword or a carried bow the first frame of the death jumped."""
    fall = dict(fall)
    s = fall.pop("_straighten", None)
    v = mix(rest, fall, ease(min(1.0, t / over)))
    if s:
        v["_straighten"] = s
    return v


def draugr_death(t):
    return _settle(DRAUGR_REST, topple(t, armed=True), t)


# --- Draugr Bowman --------------------------------------------------------------------
BOW_L, BOW_BRACE = 0.56, 0.09       # half the bow's length; how far the braced string stands off the grip
ARROW = 0.54


def _longbow(r):
    """A yew longbow as tall as his chest, gripped in the left fist: two limbs
    off the grip on joints of their own, so a full draw can bend them, and a
    string in two halves hung from the tips on joints of their own, so a pose
    can aim both at the drawing hand. Thick: at thirty pixels a bow drawn to
    scale is a hair, and it was the one thing the bowman had to have."""
    r.add("grip", E(0.042, 0.042, 0.075), "draugr_leather", "hand_l")
    for j, sz in (("limb_t", 1), ("limb_b", -1)):
        r.joint(j, (0, 0, 0), "hand_l")
        r.limb("bowlimb", (0, 0, sz * 0.03), (0, -0.012, sz * 0.30), 0.036, "draugr_bow", j, r_tip=0.03)
        r.limb("bowtip", (0, -0.012, sz * 0.30), (0, BOW_BRACE, sz * BOW_L), 0.03, "draugr_bow", j, r_tip=0.02)
        r.add("nock", E(0.022, 0.022, 0.03), "draugr_beard", j, loc=(0, BOW_BRACE, sz * BOW_L))
        s = "str_t" if sz > 0 else "str_b"
        r.joint(s, (0, BOW_BRACE, sz * BOW_L), j)
        r.limb("string", (0, 0, 0), (0, 0, -sz * BOW_L), 0.012, "draugr_string", s, r_tip=0.012)
    # The arrow on the string: out of sight until it is nocked (~arrow).
    r.joint("arrow", (0, -0.02, -0.03), "hand_r")
    r.limb("shaft", (0, 0, 0.03), (0, 0, -ARROW), 0.013, "draugr_bow", "arrow", r_tip=0.013)
    r.limb("head", (0, 0, -ARROW + 0.02), (0, 0, -ARROW - 0.07), 0.024, "draugr_iron_lt", "arrow", r_tip=0.004)
    for k in (0, 1):
        r.add("flight", E(0.03, 0.008, 0.05) if k else E(0.008, 0.03, 0.05), "draugr_fletch", "arrow",
              loc=(0, 0, 0.0))
    r.j["arrow"].scale = (0.01, 0.01, 0.01)


def build_draugr_archer():
    """The same dead, hooded and cloaked in ragged fur against the cold he no
    longer feels, a quiver of white-fletched arrows on his back and a yew
    longbow in his fist, carried upright at his side."""
    r = Rig()
    _draugr_body(r, mantle=False)
    # The hood: fur, drawn up over the head with the face in its mouth, a
    # darker ruff of it round the face and its point fallen back.
    r.add("hood", E(0.118, 0.125, 0.125), "draugr_cloak", "head", loc=(0, 0.045, 0.10))
    r.add("ruff", TORUS(0.094, 0.026), "draugr_cloak_dk", "head", loc=(0, -0.066, 0.075), rot=(math.pi / 2, 0, 0))
    r.limb("hoodpoint", (0, 0.08, 0.18), (0, 0.18, 0.25), 0.06, "draugr_cloak", "head", r_tip=0.015)
    # The cloak, from the shoulders to the calf, ragged at the hem, with a
    # collar of fur over the shoulders; on a joint of its own so it can swing.
    for k in range(7):
        a = math.radians(-90 + k * 30)
        r.limb("collar", (math.sin(a) * 0.13, 0.04 + math.cos(a) * 0.06, 0.45),
               (math.sin(a) * 0.21, 0.06 + math.cos(a) * 0.09, 0.37), 0.045,
               "draugr_cloak" if k % 2 else "draugr_cloak_dk", "chest", r_tip=0.022)
    r.joint("cape", (0, 0.12, 0.42), "chest", rest=(4, 0, 0))
    r.add("cloak", C(0.17, 0.24, 0.60, squash_y=0.45), "draugr_cloak", "cape", loc=(0, 0.0, 0.0))
    for k in range(7):
        a = math.radians(-80 + k * (160 / 6.0))
        r.add("tatter", E(0.05, 0.035, 0.08), "draugr_cloak_dk" if k % 2 else "draugr_cloak", "cape",
              loc=(math.sin(a) * 0.22, 0.03 + (1 - math.cos(a)) * 0.02 + 0.03, -0.80 - 0.04 * (k % 2)))
    # The quiver, across the cloak on his back, arrows up over his right
    # shoulder.
    r.limb("quiver", (-0.08, 0.26, 0.08), (0.09, 0.27, 0.40), 0.052, "draugr_leather", "chest", r_tip=0.056)
    for k in range(4):
        x0 = 0.07 + k * 0.022
        r.limb("q_arrow", (x0, 0.27, 0.40), (x0 + 0.035, 0.29, 0.52), 0.013, "draugr_bow", "chest", r_tip=0.013)
        r.add("q_flight", E(0.022, 0.014, 0.04), "draugr_fletch", "chest", loc=(x0 + 0.04, 0.29, 0.54))
    r.add("bracer", C(0.05, 0.048, 0.10), "draugr_leather", "elbow_l", loc=(0, 0, -0.06))
    _longbow(r)
    return r.fit("draugr_archer", 32, BOW_REST)


# All solved for this body (scratchpad bsolve.py), like the blowgunner's aim.
# At rest the bow is carried upright at his left side, its curve turned out
# to the front so it reads as a bow and not a staff.
BOW_REST = {"shoulder_l": (22, -7, -24), "elbow_l": X(-88), "hand_l": (61, 60, -39),
            "shoulder_r": fwd(8), "elbow_r": X(-20), "~arrow": -1.0}
# Bow up and out, an arrow on the string, the string just taken...
BOW_NOCK = {"chest": (0, 0, -16), "head": (0, 0, 8),
            "shoulder_l": (-90, -34, 24), "elbow_l": X(10), "hand_l": (88, 27, 24),
            "shoulder_r": (-49, -63, -32), "elbow_r": X(-90), "limb_t": X(-4), "limb_b": X(4),
            "str_t": (11.5, 2.0, 0), "~str_t": -0.047, "str_b": (-10.9, -1.8, 0), "~str_b": 0.037,
            "arrow": (79.5, -134.8, 0), "~arrow": 0.0}
# ...drawn to the cheek, the bow bending...
BOW_DRAW = {"chest": (0, 0, -16), "head": (0, 0, 8), "_y": 0.02,
            "shoulder_l": (-100, 27, 17), "elbow_l": X(12), "hand_l": (96, 40, -33),
            "shoulder_r": (-88, -37, -16), "elbow_r": X(-136), "limb_t": X(-12), "limb_b": X(12),
            "str_t": (33.9, 0.8, 0), "~str_t": -0.075, "str_b": (-30.5, -0.7, 0), "~str_b": 0.087,
            "arrow": (30.8, -179.5, 0), "~arrow": 0.0}
# ...and loosed: the string straight, the arrow gone, the hand flung back past
# the ear and the bow still up.
BOW_LOOSE = {"chest": (-4, 0, -22), "head": (0, 0, 8), "_y": 0.04,
             "shoulder_l": (-100, 27, 17), "elbow_l": X(12), "hand_l": (96, 40, -33),
             "shoulder_r": (-100, -50, -40), "elbow_r": X(-70), "~arrow": -1.0}
BOW_AFTER = {"chest": (0, 0, -10), "head": (0, 0, 4),
             "shoulder_l": (-60, 10, 0), "elbow_l": X(-30), "hand_l": (80, 50, -30),
             "shoulder_r": (-30, -20, -10), "elbow_r": X(-50), "~arrow": -1.0}


def bow_idle(t):
    s = sn(t)
    v = dict(BOW_REST)
    v.update({"_z": 0.006 * s, "chest": X(2 * s), "head": (0, 0, 7 * sn(t, 0.3)), "shoulder_r": fwd(8 + 4 * s),
              "cape": X(2 * s)})
    return v


def bow_walk(t):
    # The swordsman's tread; only the empty hand swings, and the cloak.
    v = gait(t, 34, 32, 0, 0.03, 8)
    v.update(BOW_REST)
    v.update({"shoulder_r": fwd(8 - 22 * sn(t)), "shoulder_l": (22 + 6 * sn(t), -7, -24),
              "head": (0, 0, -5 * sn(t)), "pelvis": (0, 4 * sn(t), 0), "cape": (10 + 4 * sn(t * 2), 0, 3 * sn(t))})
    return v


def bow_attack(t):
    # Every one of the six frames lands on one of these, so none of them is
    # an in-between with the string and the arrow pointing somewhere else.
    keys = (BOW_REST, BOW_NOCK, BOW_DRAW, BOW_LOOSE, BOW_AFTER, BOW_REST)
    i, k = phases(t, 0.2, 0.4, 0.6, 0.8, 1.0)
    i = min(i, 4)
    return mix(keys[i], keys[i + 1], k)


def bow_hurt(t):
    v = dict(BOW_REST)
    v.update(struck(math.sin(t * math.pi), cape=X(14)))
    return v


def bow_death(t):
    # topple() flings the left arm out, and the bow in it stood up out of the
    # body like a planted pole; laid straight down his side instead, rest
    # angles and all (topple's `armed`, for the other hand), the bow lies
    # along him.
    k = ease(t * 1.15)
    v = topple(t)
    v.update({"shoulder_l": (0, 0, 0), "elbow_l": (0, 0, 0), "hand_l": (0, 0, 0),
              "_straighten": {"shoulder_l": k, "elbow_l": k, "hand_l": k}, "cape": X(-10 * k), "~arrow": -1.0})
    return _settle(BOW_REST, v, t)


# --- Undead Warlord -------------------------------------------------------------------
def _frost(r, parent, loc, rx, ry, rz=0.03, rot=(0, 0, 0)):
    """A crust of rime lying on an upward face."""
    r.add("frost", E(rx, ry, rz), "wl_rime", parent, loc=loc, rot=rot)


def _greataxe(r, parent="hand_r"):
    """A Dane axe as long as a man: a banded haft, a broad bearded head of
    blackened iron with a frost-bright edge, a spike behind it and another on
    top. The head is carried on from the line of the forearm (-Z), the butt
    back past the fist where the other hand takes it. Its cheeks are thick
    and a spike stands off the back, so edge-on it is still an axe and not a
    line."""
    r.limb("haft", (0, 0, 0.30), (0, 0, -1.02), 0.034, "wl_haft", parent, r_tip=0.03)
    r.add("butt", E(0.045, 0.045, 0.04), "wl_iron", parent, loc=(0, 0, 0.31))
    for z in (0.20, -0.14, -0.55):
        r.add("hband", TORUS(0.036, 0.013), "wl_iron", parent, loc=(0, 0, z))
    h = -0.86
    r.add("cheek", E(0.10, 0.06, 0.13), "wl_iron_dk", parent, loc=(0.05, 0, h))
    r.add("axeblade", E(0.19, 0.035, 0.16), "wl_blade", parent, loc=(0.18, 0, h - 0.01))
    r.add("beard", E(0.09, 0.034, 0.10), "wl_blade", parent, loc=(0.27, 0, h + 0.13))
    for k in range(5):
        a = math.radians(-70 + k * 35)
        r.add("edge", E(0.03, 0.04, 0.07), "wl_edge", parent,
              loc=(0.33 + 0.05 * math.cos(a), 0, h - 0.01 + 0.19 * math.sin(a)), rot=(0, -a * 0.8, 0))
    r.limb("backspike", (-0.02, 0, h), (-0.22, 0, h + 0.03), 0.045, "wl_iron", parent, r_tip=0.008)
    r.limb("topspike", (0, 0, h - 0.10), (0, 0, h - 0.30), 0.032, "wl_iron", parent, r_tip=0.006)
    _frost(r, parent, (0.14, 0, h - 0.15), 0.12, 0.04, 0.025)


def build_undead_warlord():
    """The lords the draugr rise for: a head and a half taller than they, in
    plate gone black with age and crusted white with frost in every hollow, a
    wolf pelt over the shoulders and a cloak that was crimson hanging in rags
    behind, the long white beard of the barrow-dead out from under a great
    helm, a crown of dull gold on the helm and a stag's antlers out of the
    crown, the cold blue light in the eye-slit, and a Dane axe as long as a man
    over one shoulder."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.70))
    r.add("hips", E(0.22, 0.17, 0.14), "wl_iron_dk", "pelvis")
    humanoid_legs(r, -0.05, 0.14, 0.32, 0.32, 0.095, "wl_iron", "wl_iron_dk", foot_col="wl_iron_dk")
    for side in ("l", "r"):
        r.add("cop", E(0.08, 0.065, 0.065), "wl_iron_lt", "knee_" + side, loc=(0, -0.06, 0.0))
        r.add("greave", E(0.085, 0.09, 0.15), "wl_iron", "knee_" + side, loc=(0, -0.02, -0.15))
        _frost(r, "knee_" + side, (0, -0.07, 0.05), 0.05, 0.04, 0.02)
    r.add("tasset", C(0.23, 0.28, 0.17, squash_y=0.8), "wl_iron", "pelvis", loc=(0, 0, 0.03))
    for z in (-0.05, -0.13):
        rx = 0.23 + (0.28 - 0.23) * (-z / 0.17)
        _band(r, "pelvis", rx, rx * 0.8, z, "wl_iron_dk")
    r.add("belt", TORUS(0.23, 0.03), "wl_iron_dk", "pelvis", loc=(0, 0, 0.07))
    r.add("buckle", E(0.05, 0.035, 0.055), "bone_w", "pelvis", loc=(0, -0.225, 0.07))
    # A strip of crimson before, hanging in rags between the knees.
    r.add("fauld", E(0.10, 0.03, 0.18), "wl_crimson", "pelvis", loc=(0, -0.235, -0.16))
    for k in (-1, 0, 1):
        r.add("rag", E(0.035, 0.025, 0.06), "wl_crimson_dk" if k else "wl_crimson", "pelvis",
              loc=(k * 0.06, -0.235, -0.35 - 0.03 * abs(k)))

    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(10, 0, 0))
    r.add("torso", E(0.30, 0.22, 0.34), "wl_iron", "chest", loc=(0, 0, 0.30))
    r.add("breast", E(0.28, 0.19, 0.22), "wl_iron", "chest", loc=(0, -0.05, 0.38))
    r.add("keel", E(0.03, 0.03, 0.19), "wl_iron_lt", "chest", loc=(0, -0.235, 0.37))
    for z in (0.14, 0.22):
        rx, ry = _ring(0.30, 0.22, 0.30, 0.34, z)
        _band(r, "chest", rx, ry, z, "wl_iron_dk")
    for sx in (-1, 1):
        _frost(r, "chest", (sx * 0.13, -0.19, 0.50), 0.10, 0.05, 0.035, rot=(-0.5, sx * 0.3, 0))
    # The wolf pelt: long pale fur round the shoulders and over them.
    for k in range(11):
        a = math.radians(-110 + k * 22)
        r.limb("pelt", (math.sin(a) * 0.20, 0.02 + math.cos(a) * 0.13, 0.64),
               (math.sin(a) * 0.35, 0.06 + math.cos(a) * 0.20, 0.47), 0.075,
               "wl_fur" if k % 2 else "wl_fur_dk", "chest", r_tip=0.03)
    # The cloak, in rags, on a joint of its own so it can swing.
    r.joint("cape", (0, 0.20, 0.58), "chest", rest=(6, 0, 0))
    r.add("cloak", C(0.26, 0.36, 0.80, squash_y=0.35), "wl_crimson_dk", "cape", loc=(0, 0.0, 0.0))
    for k in range(7):
        a = math.radians(-80 + k * (160 / 6.0))
        r.add("tatter", E(0.07, 0.045, 0.11), "wl_crimson" if k % 2 else "wl_crimson_dk", "cape",
              loc=(math.sin(a) * 0.32, 0.04 + (1 - math.cos(a)) * 0.03, -1.10 - 0.05 * (k % 2)))
    humanoid_arms(r, 0.54, 0.36, 0.34, 0.33, 0.10, "wl_iron", "wl_iron_dk", flare=14)
    for side in ("l", "r"):
        sh, el = "shoulder_" + side, "elbow_" + side
        r.add("pauldron", E(0.18, 0.17, 0.13), "wl_iron", sh, loc=(0, 0, 0.05))
        r.add("lame", E(0.175, 0.165, 0.05), "wl_iron_dk", sh, loc=(0, 0, -0.05))
        _frost(r, sh, (0, 0.01, 0.16), 0.13, 0.12, 0.04)
        for k in (-1, 1):
            r.limb("icicle", (k * 0.08, -0.08, -0.07), (k * 0.08, -0.09, -0.20), 0.026, "ice", sh, r_tip=0.004)
        r.add("vambrace", C(0.085, 0.08, 0.18), "wl_iron", el, loc=(0, 0, -0.05))
        _frost(r, el, (0, -0.02, -0.02), 0.07, 0.07, 0.025)
        r.add("gauntlet", E(0.10, 0.095, 0.10), "wl_iron_dk", "hand_" + side)
    # The great helm: a barrel of black iron with an eye-slit and the light
    # in it; the beard out from under it.
    r.joint("neck", (0, -0.06, 0.64), "chest", rest=(-6, 0, 0))
    r.joint("head", (0, -0.02, 0.07), "neck", rest=(-12, 0, 0))
    r.add("beard", E(0.12, 0.07, 0.13), "draugr_beard", "head", loc=(0, -0.10, -0.10))
    for sx in (-1, 1):
        r.limb("braid", (sx * 0.05, -0.12, -0.16), (sx * 0.045, -0.12, -0.38), 0.042, "draugr_beard", "head",
               r_tip=0.03)
        r.add("ring", E(0.03, 0.03, 0.025), "wl_gold", "head", loc=(sx * 0.045, -0.12, -0.40))
    r.add("helm", E(0.155, 0.16, 0.20), "wl_iron", "head", loc=(0, 0, 0.11))
    r.add("face", E(0.13, 0.05, 0.16), "wl_iron_dk", "head", loc=(0, -0.125, 0.09))
    r.add("slit", E(0.12, 0.03, 0.024), "draugr_socket", "head", loc=(0, -0.162, 0.14))
    r.add("bar", E(0.02, 0.02, 0.12), "wl_iron_lt", "head", loc=(0, -0.172, 0.07))
    for sx in (-1, 1):
        r.add("eye", E(0.038, 0.02, 0.02), "draugr_eye_glow", "head", loc=(sx * 0.052, -0.172, 0.14))
    # The crown, tight to the helm -- stood off it, from above it was a halo --
    # and the antlers out of it, swept back so from the side they are antlers
    # and not a stick.
    r.add("crown", TORUS(0.145, 0.028), "wl_gold", "head", loc=(0, 0, 0.19))
    for k in range(6):
        a = k * math.tau / 6
        r.limb("crownpt", (math.sin(a) * 0.145, -math.cos(a) * 0.145, 0.20),
               (math.sin(a) * 0.15, -math.cos(a) * 0.15, 0.29), 0.028, "wl_gold", "head", r_tip=0.006)
    for sx in (-1, 1):
        r.limb("beam", (sx * 0.12, 0.04, 0.24), (sx * 0.28, 0.14, 0.38), 0.04, "wl_antler", "head", r_tip=0.034)
        r.limb("beam2", (sx * 0.28, 0.14, 0.38), (sx * 0.40, 0.24, 0.56), 0.034, "wl_antler", "head", r_tip=0.014)
        r.limb("brow_tine", (sx * 0.16, 0.06, 0.28), (sx * 0.22, -0.10, 0.34), 0.025, "wl_antler_dk", "head",
               r_tip=0.006)
        r.limb("tine", (sx * 0.22, 0.10, 0.33), (sx * 0.25, 0.02, 0.50), 0.024, "wl_antler", "head", r_tip=0.006)
        r.limb("tine", (sx * 0.33, 0.19, 0.47), (sx * 0.32, 0.12, 0.64), 0.022, "wl_antler", "head", r_tip=0.006)
        r.limb("tine", (sx * 0.37, 0.22, 0.52), (sx * 0.54, 0.22, 0.58), 0.022, "wl_antler", "head", r_tip=0.006)
        r.add("rime", E(0.03, 0.03, 0.02), "wl_rime", "head", loc=(sx * 0.28, 0.14, 0.41))
    # Fitted on the man, antlers and all; the axe stands a head over him and
    # fitting the two together shrank him to make room for it (the
    # blowgunner's lesson). 54 rather than 50: swept back, the antlers stand
    # higher on screen, and at 50 the man under them came out smaller than a
    # lord should be.
    r.fit("undead_warlord", 54, WARLORD_REST)
    _greataxe(r)
    return r


# Solved for this body (scratchpad wsolve.py). The axe over his right
# shoulder, the fist before the chest, the blade turned out so it shows its
# face from the front.
WARLORD_REST = {"shoulder_r": (7, 18, -11), "elbow_r": X(-112), "hand_r": (-104, 0, -8),
                "shoulder_l": fwd(10), "elbow_l": X(-24)}
# Both hands on the haft, taking it up past his face...
WARLORD_LIFT = {"chest": (-4, 0, -10), "head": X(-4),
                "shoulder_r": (-84, -14, -17), "elbow_r": X(-106), "hand_r": (3, -16, -16),
                "shoulder_l": (-99, 63, 32), "elbow_l": X(-39)}
# ...over his head, the head of it hanging down his back...
WARLORD_RAISE = {"chest": X(-12), "head": X(-6), "_y": 0.04, "_z": 0.02,
                 "shoulder_r": (-177, -8, 38), "elbow_r": X(-24), "hand_r": (-99, 52, 13),
                 "shoulder_l": (-164, 19, 4), "elbow_l": X(10), "cape": X(14)}
# ...and down through whatever is in front of him, the whole of him behind it...
# (Stopped at the height of a man's knee: followed further, from the front the
# spike on top of the axe hung out of the bottom of the frame.)
WARLORD_CLEAVE = {"chest": X(26), "head": X(-4), "_y": -0.06, "_z": -0.03,
                  "shoulder_r": (-43, 38, -48), "elbow_r": X(-80), "hand_r": (-43, -60, 80),
                  "shoulder_l": (-58, 49, 52), "elbow_l": X(-89),
                  "hip_l": fwd(30), "hip_r": fwd(-18), "knee_r": X(22), "cape": X(-20)}
# ...into the ground.
WARLORD_FOLLOW = {"chest": X(32), "head": X(-8), "_y": -0.06, "_z": -0.05,
                  "shoulder_r": (-25, 56, -39), "elbow_r": X(-84), "hand_r": (-19, -45, 50),
                  "shoulder_l": (-33, -56, 70), "elbow_l": X(-90),
                  "hip_l": fwd(30), "hip_r": fwd(-18), "knee_l": X(10), "knee_r": X(26), "cape": X(-24)}
# (The cloak hangs from his chest, so it is turned back by as much as the
# chest leans in: left alone it stood out behind him like a board.)
# How the barrow-dead are laid out: the axe held up along him from a fist on
# his chest, its head past his crown.
WARLORD_LAID = {"shoulder_r": (-19, 15, -49), "elbow_r": X(-100), "hand_r": (-36, -26, -46),
                "shoulder_l": fwd(20), "elbow_l": X(-60)}


def warlord_idle(t):
    s = sn(t)
    v = dict(WARLORD_REST)
    v.update({"_z": 0.005 * s, "chest": X(2 * s), "head": (0, 0, 4 * sn(t, 0.3)), "shoulder_l": fwd(10 + 4 * s),
              "cape": X(2 * s)})
    return v


def warlord_walk(t):
    v = gait(t, 28, 28, 0, 0.03, 8)
    v.update(WARLORD_REST)
    v.update({"shoulder_l": fwd(10 - 20 * sn(t)), "pelvis": (0, 5 * sn(t), 0), "head": (0, 0, -4 * sn(t)),
              "cape": (12 + 4 * sn(t * 2), 0, 3 * sn(t))})
    return v


def warlord_attack(t):
    # One solved key to a frame: a two-handed axe blended between keys lets go
    # of itself, the left hand drifting off the haft.
    keys = (WARLORD_REST, WARLORD_LIFT, WARLORD_RAISE, WARLORD_CLEAVE, WARLORD_FOLLOW, WARLORD_REST)
    i, k = phases(t, 0.2, 0.4, 0.6, 0.8, 1.0)
    i = min(i, 4)
    return mix(keys[i], keys[i + 1], k)


def warlord_hurt(t):
    v = dict(WARLORD_REST)
    v.update(struck(math.sin(t * math.pi) * 0.8, cape=X(14)))
    return v


def warlord_death(t):
    # To his knees with the axe brought up to his chest, then over onto his
    # side with it laid along him. Fallen the way topple() lays a weapon arm,
    # the axe went on past his feet, through the floor and out of the frame.
    k = ease(min(1.0, t * 1.8))
    v = mix(WARLORD_REST, WARLORD_LAID, k)
    v.update({"hip_l": fwd(60 * k), "knee_l": X(70 * k), "hip_r": fwd(-8 * k), "knee_r": X(80 * k),
              "_z": -0.16 * k, "chest": X(14 * k), "cape": X(-12 * k)})
    if t > 0.45:
        w = topple((t - 0.45) / 0.55, dir=-1)
        for key, val in w.items():
            if key in ("shoulder_r", "elbow_r", "hand_r", "_straighten"):
                continue
            if isinstance(val, tuple):
                base = v.get(key, (0.0, 0.0, 0.0))
                v[key] = tuple(a + b for a, b in zip(base, val))
            else:
                v[key] = v.get(key, 0.0) + val
    return v


# =================================================================================
#  The Frostback Troll
# =================================================================================

def _perp(d):
    """Some direction square to d, leaning towards the camera side (-Y) and up."""
    p = Vector((0, -0.7, 0.7)).cross(d)
    if p.length < 1e-4:
        p = Vector((1, 0, 0)).cross(d)
    return d.cross(p).normalized()


def _crystal(r, parent, base, d, length, width):
    """One shard of ice: a deep blue spike, and a lit facet down the side of it
    that faces up and out, so it reads as ice and not as a horn."""
    base, d = Vector(base), Vector(d).normalized()
    tip = base + d * length
    r.limb("crystal", tuple(base), tuple(tip), width, "fb_crystal", parent, r_tip=width * 0.1)
    off = _perp(d) * width * 0.45
    r.limb("facet", tuple(base + off + d * length * 0.1), tuple(base + off * 0.4 + d * length * 0.86),
           width * 0.42, "fb_crystal_glow", parent, r_tip=0.004)
    r.add("crystal_root", E(width * 1.1, width * 1.1, width * 0.8), "fb_crystal_dk", parent, loc=tuple(base))


def _cluster(r, parent, base, normal, sizes, spread=26, turn=0.0, width=0.07):
    """Shards grown out of one place, fanned round `normal`, longest first."""
    n = Vector(normal).normalized()
    a = _perp(n)
    b = n.cross(a).normalized()
    for k, length in enumerate(sizes):
        if k == 0:
            d = n
        else:
            ang = turn + (k - 1) * math.tau / max(1, len(sizes) - 1)
            t = math.radians(spread)
            d = (n * math.cos(t) + (a * math.cos(ang) + b * math.sin(ang)) * math.sin(t)).normalized()
        _crystal(r, parent, Vector(base) + (d - n) * width * 0.8, d, length, width * (1.0 if k == 0 else 0.8))


def _shag(r, parent, root, tip, width, k=0):
    """A clump of long fur."""
    r.limb("shag", root, tip, width, "fb_fur" if k % 3 else "fb_fur_dk", parent, r_tip=width * 0.3)


def build_frostback_troll():
    """The Ice Troll's older, bigger cousin, from higher up: hunched so far its
    head hangs below its shoulders, shaggy with white fur over pale blue hide,
    arms to its knees and fists like boulders -- and the ice grown into it:
    jagged shards of deep blue out of its hump, its shoulders and the backs of
    its forearms, and two on its brow. The shards are what tell it from the
    Ice Troll at forty pixels, so they are big, blue, and lit."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.60))
    r.add("hips", E(0.30, 0.24, 0.20), "fb_skin", "pelvis")
    r.add("kilt", C(0.29, 0.33, 0.14, squash_y=0.8), "fb_hide", "pelvis", loc=(0, 0, 0.03))
    for k in range(7):
        a = math.radians(-90 + k * 30)
        r.add("tatter", E(0.065, 0.045, 0.08), "fb_hide_dk" if k % 2 else "fb_hide", "pelvis",
              loc=(math.sin(a) * 0.30, -math.cos(a) * 0.24, -0.18 - 0.03 * (k % 2)))
    r.add("belt", TORUS(0.31, 0.03), "fb_hide_dk", "pelvis", loc=(0, 0, 0.08))
    humanoid_legs(r, -0.06, 0.19, 0.28, 0.27, 0.14, "fb_skin", "fb_skin_dk", foot_col="fb_skin_dk")
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        for k in range(4):
            a = math.radians(-60 + k * 50) * sx
            _shag(r, "hip_" + side, (math.sin(a) * 0.10, math.cos(a) * 0.08, -0.06),
                  (math.sin(a) * 0.16, math.cos(a) * 0.12, -0.30), 0.065, k)
        for k in (-1, 0, 1):
            r.limb("toenail", (k * 0.06, -0.20, -0.28), (k * 0.07, -0.26, -0.30), 0.028, "fb_nail",
                   "knee_" + side, r_tip=0.008)

    r.joint("chest", (0, 0, 0.12), "pelvis", rest=(28, 0, 0))
    r.add("torso", E(0.48, 0.36, 0.44), "fb_skin", "chest", loc=(0, 0, 0.38))
    r.add("gut", E(0.32, 0.22, 0.26), "fb_skin_lt", "chest", loc=(0, -0.17, 0.18))
    r.add("pecs", E(0.36, 0.17, 0.14), "fb_skin_lt", "chest", loc=(0, -0.20, 0.50))
    # The hump, under its fur, and the shag hanging off it down the back and
    # round the shoulders.
    # (Hung straight down the back, not fanned out round the hump: fanned, from
    # the side it was a hedgehog.)
    r.add("hump", E(0.44, 0.32, 0.30), "fb_fur", "chest", loc=(0, 0.14, 0.64))
    for row, (y, z0, z1, n) in enumerate(((0.38, 0.78, 0.36, 5), (0.44, 0.56, 0.10, 6))):
        for k in range(n):
            x = (k - (n - 1) / 2.0) * (0.60 / (n - 1))
            _shag(r, "chest", (x, y, z0), (x * 1.15, y + 0.06, z1 - 0.04 * (k % 2)), 0.085, k + row)
    for sx in (-1, 1):
        for k in range(3):
            _shag(r, "chest", (sx * (0.34 + k * 0.04), 0.20 - k * 0.08, 0.66),
                  (sx * (0.44 + k * 0.04), 0.26 - k * 0.08, 0.26), 0.08, k)
    # The ice: a crown of it out of the hump, a ridge of it down the spine.
    _cluster(r, "chest", (0, 0.30, 0.80), (0, 0.55, 0.85), (0.52, 0.40, 0.36, 0.30, 0.34), spread=30, width=0.085)
    _cluster(r, "chest", (0.22, 0.32, 0.66), (0.5, 0.7, 0.5), (0.34, 0.24, 0.22), spread=28, width=0.07, turn=1.0)
    _cluster(r, "chest", (-0.22, 0.32, 0.66), (-0.5, 0.7, 0.5), (0.34, 0.24, 0.22), spread=28, width=0.07, turn=2.0)
    _cluster(r, "chest", (0, 0.38, 0.40), (0, 0.95, 0.30), (0.28, 0.20), spread=30, width=0.065)

    humanoid_arms(r, 0.66, 0.52, 0.43, 0.43, 0.16, "fb_skin", "fb_skin_dk", flare=18)
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        sh, el, hd = "shoulder_" + side, "elbow_" + side, "hand_" + side
        r.add("deltoid", E(0.19, 0.18, 0.17), "fb_fur_dk", sh, loc=(0, 0, 0.02))
        for k in range(4):
            a = math.radians(-70 + k * 50)
            _shag(r, sh, (math.sin(a) * 0.15 * sx, math.cos(a) * 0.12, -0.02),
                  (math.sin(a) * 0.22 * sx, math.cos(a) * 0.16, -0.30), 0.07, k)
        _cluster(r, sh, (sx * 0.10, 0.06, 0.16), (sx * 0.55, 0.35, 0.75), (0.34, 0.24, 0.20), spread=30, width=0.07)
        # Fur down the back of the forearm, and the ice through it.
        for k in range(3):
            _shag(r, el, (sx * 0.06, 0.12, -0.06 - k * 0.12), (sx * 0.10, 0.22, -0.18 - k * 0.12), 0.06, k)
        _crystal(r, el, (sx * 0.05, 0.14, -0.14), (sx * 0.35, 0.90, 0.25), 0.26, 0.06)
        _crystal(r, el, (sx * 0.07, 0.12, -0.30), (sx * 0.55, 0.80, 0.10), 0.20, 0.05)
        r.add("fist", E(0.19, 0.18, 0.19), "fb_skin_dk", hd, loc=(0, -0.02, -0.05))
        for k in range(4):
            r.add("knuckle", E(0.045, 0.04, 0.04), "fb_skin_lt", hd, loc=((k - 1.5) * 0.085, -0.17, -0.10))

    # The head: hung forward and low under the hump, a brow like a ledge, a
    # jutting jaw with the tusks up out of it, small eyes lit from inside.
    # Far enough forward to come out from under the hump: from the side,
    # further in, the fur swallowed it.
    r.joint("neck", (0, -0.34, 0.72), "chest", rest=(-26, 0, 0))
    r.joint("head", (0, -0.04, 0.08), "neck", rest=(-14, 0, 0))
    r.add("skull", E(0.21, 0.20, 0.18), "fb_skin", "head", loc=(0, 0, 0.10))
    r.add("brow", E(0.22, 0.09, 0.06), "fb_skin_dk", "head", loc=(0, -0.15, 0.17))
    r.add("jaw", E(0.20, 0.17, 0.10), "fb_skin_dk", "head", loc=(0, -0.10, -0.03))
    r.add("mouth", E(0.14, 0.03, 0.022), "fb_mouth", "head", loc=(0, -0.255, 0.01))
    r.add("nose", E(0.06, 0.07, 0.06), "fb_skin_dk", "head", loc=(0, -0.21, 0.09))
    for sx in (-1, 1):
        r.add("eye", E(0.045, 0.03, 0.035), "fb_eye_glow", "head", loc=(sx * 0.09, -0.18, 0.13))
        r.limb("tusk", (sx * 0.11, -0.22, -0.04), (sx * 0.17, -0.31, 0.15), 0.048, "fb_tusk", "head", r_tip=0.012)
        r.limb("ear", (sx * 0.18, 0.02, 0.12), (sx * 0.31, 0.08, 0.20), 0.05, "fb_skin", "head", r_tip=0.012)
        _crystal(r, "head", (sx * 0.10, -0.04, 0.24), (sx * 0.35, 0.10, 0.93), 0.20, 0.05)
    for k in range(5):
        x = (k - 2) * 0.08
        _shag(r, "head", (x, 0.02, 0.24), (x * 1.3, 0.22, 0.20), 0.06, k)
    return r.fit("frostback_troll", 56, FROST_REST)


# Hunched and heavy: the fists hanging forward by its knees.
FROST_REST = {"shoulder_l": (-18, 0, 6), "elbow_l": X(-20), "shoulder_r": (-18, 0, -6), "elbow_r": X(-20),
              "hand_l": X(-10), "hand_r": X(-10)}
# Both fists up over the hump, rearing back onto its heels...
FROST_RAISE = {"chest": X(-26), "neck": X(-8), "head": X(-4), "_y": 0.06, "_z": 0.02,
               "shoulder_l": (-165, 0, 22), "elbow_l": X(-30), "shoulder_r": (-165, 0, -22), "elbow_r": X(-30),
               "hip_l": fwd(-6), "hip_r": fwd(-6)}
# ...and both brought down together on whatever is in front of it. (The
# shoulders turn through the chest's own lean as well: at -52 the arms only
# hung straight down under it and the blow landed on its own feet.)
FROST_SMASH = {"chest": X(22), "neck": X(4), "head": X(-8), "_y": -0.18, "_z": -0.06,
               "shoulder_l": (-95, 0, 14), "elbow_l": X(-6), "shoulder_r": (-95, 0, -14), "elbow_r": X(-6),
               "hand_l": X(-20), "hand_r": X(-20),
               "hip_l": fwd(32), "hip_r": fwd(-18), "knee_l": X(26), "knee_r": X(20)}


def frost_idle(t):
    s = sn(t)
    v = dict(FROST_REST)
    v.update({"_z": 0.012 * s, "chest": X(3 * s), "head": (0, 0, 6 * sn(t, 0.3)), "neck": X(-2 * s),
              "shoulder_l": (-18 + 4 * s, 0, 6), "shoulder_r": (-18 - 4 * s, 0, -6)})
    return v


def frost_walk(t):
    # A heavy, rolling walk: short strides, the weight thrown over each one,
    # the arms swinging from the shoulders like ropes.
    v = gait(t, 26, 24, 0, 0.05, 4)
    v.update(FROST_REST)
    v.update({"pelvis": (0, 8 * sn(t), 0), "chest": (2, 0, 7 * sn(t)), "head": (0, 0, -7 * sn(t)),
              "shoulder_l": (-18 - 20 * sn(t), 0, 6), "shoulder_r": (-18 + 20 * sn(t), 0, -6)})
    return v


def frost_attack(t):
    return swing(t, FROST_REST, FROST_RAISE, FROST_SMASH, (0.44, 0.60, 1.0))


def frost_hurt(t):
    v = dict(FROST_REST)
    v.update(struck(math.sin(t * math.pi) * 0.8, shoulder_l=(-30, 0, 20), shoulder_r=(-30, 0, -20)))
    return v


def frost_death(t):
    # Over sideways, the arms laid along it: flung forward the way topple()
    # throws them, fists this size end up in the frame below.
    k = ease(t * 1.15)
    v = topple(t, dir=-1)
    v.update({"shoulder_l": fwd(14 * k), "shoulder_r": fwd(10 * k), "elbow_l": X(-24 * k), "elbow_r": X(-20 * k),
              "chest": X(-10 * k)})
    # Lying a little higher, as the Shellback Elder does: a hump that size on
    # its side reached down to the bottom of the frame.
    v["_wz"] = v.get("_wz", 0.0) + 0.08 * k
    return _settle(FROST_REST, v, t)


# =================================================================================
#  The Abominable Snowman
# =================================================================================

def _yshag(r, parent, root, tip, w, k=0):
    """A long clump of the Snowman's fur: white, and blue-grey in the ones that
    hang in shadow, so the whole coat is not one flat white."""
    col = ("yeti_fur", "yeti_fur_md", "yeti_fur", "yeti_fur_dk")[k % 4]
    r.limb("shag", root, tip, w, col, parent, r_tip=w * 0.25)


def _hang(r, parent, cx, cy, z0, z1, rx, ry, n, w, a0=-90, a1=90, drop=0.0, k0=0, flare=1.25):
    """A skirt of clumps round an ellipse (centre cx,cy, radii rx,ry) at height
    z0, hanging to z1 and flaring out by `flare` as they go, from angle a0 to
    a1 (0 is straight back, +-90 the sides)."""
    for k in range(n):
        a = math.radians(a0 + (a1 - a0) * k / max(1, n - 1))
        x, y = math.sin(a), math.cos(a)
        _yshag(r, parent, (cx + x * rx, cy + y * ry, z0),
               (cx + x * rx * flare, cy + y * ry * flare, z1 - drop * (k % 2)), w, k + k0)


def build_abominable_snowman():
    """The Snowman, which is seen once a winter if that: a great white ape of
    the high snow, twice a man's height, its coat hanging in long clumps that
    go blue in their shadows, a peaked crown of fur, a leathery blue-grey face
    with a brow like a ledge and small eyes like coals under it, a mouth of
    fangs, and arms that reach below its knees to big dark-palmed hands with
    claws on them."""
    r = Rig()
    # Long in the leg for an ape: built on a troll's legs it was a snowball
    # with a face in it.
    r.joint("pelvis", (0, 0, 0.80))
    r.add("hips", E(0.36, 0.30, 0.24), "yeti_fur_md", "pelvis")
    humanoid_legs(r, -0.06, 0.22, 0.37, 0.35, 0.17, "yeti_fur", "yeti_fur_md", foot_col="yeti_palm")
    for side in ("l", "r"):
        _hang(r, "hip_" + side, 0, 0, -0.04, -0.44, 0.15, 0.14, 5, 0.09, a0=-180, a1=180, drop=0.04, k0=side == "r")
        _hang(r, "knee_" + side, 0, 0, -0.04, -0.26, 0.12, 0.12, 5, 0.07, a0=-180, a1=180, k0=1)
        for k in range(4):
            x = (k - 1.5) * 0.065
            r.limb("toeclaw", (x, -0.26, -0.34), (x * 1.1, -0.34, -0.36), 0.03, "yeti_claw", "knee_" + side,
                   r_tip=0.008)
    # The coat over the loins, front and back.
    _hang(r, "pelvis", 0, 0, 0.02, -0.34, 0.34, 0.28, 9, 0.10, a0=-180, a1=180, drop=0.05)

    r.joint("chest", (0, 0, 0.14), "pelvis", rest=(14, 0, 0))
    r.add("torso", E(0.56, 0.42, 0.52), "yeti_fur", "chest", loc=(0, 0, 0.42))
    r.add("belly", E(0.38, 0.26, 0.30), "yeti_fur_md", "chest", loc=(0, -0.18, 0.20))
    r.add("hump", E(0.50, 0.34, 0.30), "yeti_fur", "chest", loc=(0, 0.14, 0.76))
    # The coat: clumps down the chest, down the sides and down the back.
    _hang(r, "chest", 0, 0, 0.62, 0.14, 0.44, 0.38, 13, 0.11, a0=-160, a1=160, drop=0.06)
    _hang(r, "chest", 0, 0.04, 0.86, 0.46, 0.38, 0.30, 9, 0.10, a0=-120, a1=120, drop=0.05, k0=2)

    humanoid_arms(r, 0.80, 0.60, 0.52, 0.50, 0.19, "yeti_fur", "yeti_fur_md", flare=20)
    for side in ("l", "r"):
        sx = -1 if side == "l" else 1
        sh, el, hd = "shoulder_" + side, "elbow_" + side, "hand_" + side
        r.add("deltoid", E(0.21, 0.21, 0.21), "yeti_fur", sh, loc=(0, 0, 0.0))
        # Long fur off the arm, hanging to past the elbow, and a ruff of it
        # at the wrist over the hand.
        # (Close to the arm: flared out like the coat, from behind the arms
        # were a pair of wings.)
        _hang(r, sh, 0, 0, -0.06, -0.52, 0.17, 0.16, 7, 0.085, a0=-150, a1=150, drop=0.05, k0=1, flare=1.0)
        _hang(r, el, 0, 0, -0.06, -0.50, 0.14, 0.14, 7, 0.075, a0=-150, a1=150, drop=0.04, flare=1.0)
        # The hand: a broad dark palm, four fingers and a thumb, pale claws.
        r.add("palm", E(0.15, 0.10, 0.16), "yeti_palm", hd, loc=(0, -0.01, -0.10))
        for k in range(4):
            x = (k - 1.5) * 0.07
            r.limb("finger", (x, -0.04, -0.18), (x * 1.25, -0.11, -0.34), 0.048, "yeti_palm", hd, r_tip=0.04)
            r.limb("claw", (x * 1.25, -0.11, -0.34), (x * 1.3, -0.18, -0.44), 0.036, "yeti_claw", hd, r_tip=0.006)
        r.limb("thumb", (-sx * 0.12, -0.06, -0.08), (-sx * 0.16, -0.16, -0.20), 0.045, "yeti_palm", hd, r_tip=0.036)
        r.limb("thumbclaw", (-sx * 0.16, -0.16, -0.20), (-sx * 0.16, -0.23, -0.27), 0.032, "yeti_claw", hd,
               r_tip=0.006)

    # The head, sunk between the shoulders but carried forward of them: a
    # peaked crown of fur, and the face -- the one part of it that is not
    # white -- pushed out into a muzzle, so from the side it has a profile.
    r.joint("neck", (0, -0.30, 0.86), "chest", rest=(-14, 0, 0))
    r.joint("head", (0, -0.04, 0.06), "neck", rest=(-12, 0, 0))
    r.add("skull", E(0.27, 0.26, 0.26), "yeti_fur", "head", loc=(0, 0.02, 0.16))
    for k in range(5):
        x = (k - 2) * 0.07
        _yshag(r, "head", (x, 0.04, 0.30), (x * 0.4, 0.14, 0.60 - abs(k - 2) * 0.06), 0.11, k)
    r.add("face", E(0.21, 0.10, 0.20), "yeti_face", "head", loc=(0, -0.22, 0.10))
    r.add("muzzle", E(0.14, 0.10, 0.09), "yeti_face", "head", loc=(0, -0.29, 0.04))
    r.add("brow", E(0.22, 0.10, 0.06), "yeti_face_dk", "head", loc=(0, -0.26, 0.21))
    r.add("browfur", E(0.22, 0.10, 0.05), "yeti_fur", "head", loc=(0, -0.16, 0.29))
    r.add("nose", E(0.07, 0.05, 0.05), "yeti_face_dk", "head", loc=(0, -0.37, 0.10))
    for sx in (-1, 1):
        r.add("socket", E(0.05, 0.02, 0.036), "yeti_face_dk", "head", loc=(sx * 0.08, -0.29, 0.165))
        r.add("eye", E(0.035, 0.02, 0.025), "yeti_eye_glow", "head", loc=(sx * 0.08, -0.305, 0.165))
    r.joint("jaw", (0, -0.10, 0.02), "head")
    r.add("mouth", E(0.13, 0.05, 0.05), "yeti_mouth", "head", loc=(0, -0.345, 0.0))
    r.add("lowjaw", E(0.15, 0.13, 0.06), "yeti_face", "jaw", loc=(0, -0.18, -0.05))
    for sx in (-1, 1):
        r.limb("fang", (sx * 0.08, -0.355, 0.05), (sx * 0.08, -0.375, -0.05), 0.026, "yeti_fang", "head",
               r_tip=0.006)
        r.limb("tusk", (sx * 0.06, -0.28, -0.03), (sx * 0.06, -0.31, 0.04), 0.02, "yeti_fang", "jaw", r_tip=0.005)
        # Cheek fur hanging off the jaw.
        for k in range(2):
            _yshag(r, "jaw", (sx * (0.13 + k * 0.05), -0.08 + k * 0.04, -0.02),
                   (sx * (0.17 + k * 0.06), -0.08 + k * 0.05, -0.26), 0.07, k + 1)
    return r.fit("abominable_snowman", 72, YETI_REST)


YETI_REST = {"shoulder_l": (-12, 0, 6), "elbow_l": X(-18), "shoulder_r": (-12, 0, -6), "elbow_r": X(-18),
             "hand_l": X(-10), "hand_r": X(-10)}
# Up on its hind legs, both fists raised either side of its head, roaring.
# (Elbows bent: straight up, arms this long put the claws over the top of the
# frame.)
YETI_REAR = {"chest": X(-20), "neck": X(-12), "head": X(-6), "jaw": X(26), "_z": 0.03, "_y": 0.05,
             "shoulder_l": (-150, 0, 30), "elbow_l": X(-70), "shoulder_r": (-150, 0, -30), "elbow_r": X(-70),
             "hand_l": X(-20), "hand_r": X(-20), "hip_l": fwd(-8), "hip_r": fwd(-8)}
# ...and down on the ground in front of it with the whole weight of it. (The
# shoulders turn through the chest's lean too; any further forward and from
# the side the claws were out of the frame.)
YETI_SLAM = {"chest": X(24), "neck": X(6), "head": X(-10), "jaw": X(12), "_y": -0.06, "_z": -0.10,
             "shoulder_l": (-66, 0, 12), "elbow_l": X(-6), "shoulder_r": (-66, 0, -12), "elbow_r": X(-6),
             "hand_l": X(24), "hand_r": X(24),
             "hip_l": fwd(30), "hip_r": fwd(-16), "knee_l": X(30), "knee_r": X(26)}


def yeti_idle(t):
    s = sn(t)
    v = dict(YETI_REST)
    v.update({"_z": 0.012 * s, "chest": X(3 * s), "neck": X(-3 * s), "head": (0, 0, 7 * sn(t, 0.3)),
              "jaw": X(6 * max(0.0, sn(t, 0.2))),
              "shoulder_l": (-12 + 4 * s, 0, 6), "shoulder_r": (-12 - 4 * s, 0, -6)})
    return v


def yeti_walk(t):
    # Slower and heavier than a troll's: short strides with the whole bulk
    # rolled over each one, the head low, the long arms swinging.
    v = gait(t, 22, 24, 0, 0.05, 8)
    v.update(YETI_REST)
    v.update({"pelvis": (0, 10 * sn(t), 0), "chest": (8, 0, 9 * sn(t)), "head": (0, 0, -8 * sn(t)),
              "shoulder_l": (-12 - 22 * sn(t), 0, 6), "shoulder_r": (-12 + 22 * sn(t), 0, -6),
              "_z": 0.04 * abs(sn(t)) - 0.02})
    return v


def yeti_attack(t):
    return swing(t, YETI_REST, YETI_REAR, YETI_SLAM, (0.46, 0.62, 1.0))


def yeti_hurt(t):
    v = dict(YETI_REST)
    v.update(struck(math.sin(t * math.pi) * 0.7, shoulder_l=(-30, 0, 20), shoulder_r=(-30, 0, -20), jaw=X(22)))
    return v


def yeti_death(t):
    # Over sideways, as the Frostback goes, but with the arms swung back and
    # the hands curled: laid forward along it, the claws of the lower hand
    # pointed at the camera and hung out of the bottom of the frame.
    k = ease(t * 1.15)
    v = topple(t, dir=-1)
    v.update({"shoulder_l": fwd(-16 * k), "shoulder_r": fwd(-16 * k), "elbow_l": X(-30 * k), "elbow_r": X(-30 * k),
              "hand_l": X(-50 * k), "hand_r": X(-50 * k), "chest": X(-10 * k), "jaw": X(20 * k)})
    v["_wz"] = v.get("_wz", 0.0) + 0.10 * k
    return _settle(YETI_REST, v, t)


# =================================================================================
#  Registration
# =================================================================================

# id: (builder, frame px, (idle, walk, attack, hurt, death), shadow radius)
CREATURES = {
    "draugr": (build_draugr, 72, (draugr_idle, draugr_walk, draugr_attack, draugr_hurt, draugr_death), 0.32),
    "draugr_archer": (build_draugr_archer, 72, (bow_idle, bow_walk, bow_attack, bow_hurt, bow_death), 0.32),
    "undead_warlord": (build_undead_warlord, 112,
                       (warlord_idle, warlord_walk, warlord_attack, warlord_hurt, warlord_death), 0.50),
    "frostback_troll": (build_frostback_troll, 112,
                        (frost_idle, frost_walk, frost_attack, frost_hurt, frost_death), 0.60),
    "abominable_snowman": (build_abominable_snowman, 144,
                           (yeti_idle, yeti_walk, yeti_attack, yeti_hurt, yeti_death), 0.75),
}


def register():
    cr.CREATURES.update(CREATURES)
