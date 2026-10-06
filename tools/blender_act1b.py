# =============================================================================
#  blender_act1b.py - more of what Act I sends at the player in the Reverie:
#  Wynn's nightmare, the Shear Mannequin.
#
#  Rendered by tools/make_creatures.ps1 like every other monster:
#      .\tools\make_creatures.ps1 -Only shear_mannequin
#
#  Built the way blender_act1.py's bosses are -- the prologue's Rig, the act's
#  rounded blocks and lathe, fitted to a height on screen -- registered into
#  blender_creatures.CREATURES, and its own clips (the spin and the snip) into
#  EXTRA_CLIPS. blender_creatures.py hands itself over at its foot, after
#  blender_act1.
#
#  The gown's three colours are the ones the waking shop's threads, the dress
#  form and the portrait use (tools/blender_act1_props.py, "thr_*"): the same
#  values, kept in step by hand because the two files are run by different
#  Blender sessions.
# =============================================================================

import math

from mathutils import Vector

import blender_character as bc
import blender_creatures as cr
import blender_prologue as pr
import blender_act1 as a1

E, C = cr.E, cr.C
X, fwd, sn, ease, phases, mix = cr.X, cr.fwd, cr.sn, cr.ease, cr.phases, cr.mix
TORUS = bc.mesh_torus
FRUSTUM = bc.mesh_frustum
Rig = pr.Rig
rbox, lathe, fitted = a1.rbox, a1.lathe, a1.fitted


# --- colours ---------------------------------------------------------------------------
bc.PALETTE.update({
    # The gown: a crimson bodice, an ivory skirt, a gold sash -- exactly the
    # thread colours of the waking shop (blender_act1_props.THREADS), so the
    # threads the player cuts are the dress that comes alive.
    "sm_crimson": (0.70, 0.10, 0.14), "sm_crimson_dk": (0.50, 0.06, 0.10),
    "sm_ivory": (0.93, 0.89, 0.78), "sm_ivory_dk": (0.78, 0.73, 0.62),
    "sm_gold": (0.86, 0.66, 0.20), "sm_gold_dk": (0.64, 0.46, 0.12),
    # The form: pale turned wood for the knob, the neck and the arms, darker
    # at the joints, oak for the stand and brass on its three feet.
    "sm_wood": (0.78, 0.64, 0.46), "sm_wood_dk": (0.55, 0.41, 0.27), "sm_oak": (0.46, 0.30, 0.18),
    "sm_brass": (0.80, 0.62, 0.24),
    # The blades: a dark back and a bright edge, and the pins' glint, which is
    # flat light so it stays a bright pixel in every band.
    "sm_steel_dk": (0.40, 0.43, 0.50), "sm_edge": (0.90, 0.93, 0.98), "sm_pin_glow": (1.00, 0.99, 0.92),
    "sm_pin": (0.62, 0.65, 0.70), "sm_void": (0.10, 0.07, 0.09),
})


# --- the shapes ------------------------------------------------------------------------
# The full skirt, waist to hem, (radius, z) under the waist: a bell, belled
# out low, its hem well up off the floor so the stand's feet show under it.
SKIRT = ((0.001, 0.06), (0.16, 0.06), (0.195, 0.02), (0.22, -0.06), (0.26, -0.18), (0.30, -0.30),
         (0.345, -0.42), (0.39, -0.54), (0.43, -0.66), (0.455, -0.74), (0.46, -0.77), (0.445, -0.79),
         (0.39, -0.795), (0.001, -0.795))

# The form's padded torso, laced into the bodice: a narrow waist, the bust,
# the shoulders squared off where the arms come out. Flattened front to back.
BODICE = ((0.001, 0.475), (0.11, 0.475), (0.21, 0.455), (0.265, 0.41), (0.265, 0.34), (0.25, 0.27),
          (0.22, 0.19), (0.185, 0.11), (0.168, 0.04), (0.172, -0.005), (0.001, -0.005))
BODICE_FLAT = 0.74


def _bell(profile, z):
    """The radius of a lathe profile at height z (between its points)."""
    pts = [p for p in profile if p[0] > 0.01]
    for (r0, z0), (r1, z1) in zip(pts, pts[1:]):
        if (z0 - z) * (z1 - z) <= 0 and z0 != z1:
            return r0 + (r1 - r0) * (z - z0) / (z1 - z0)
    return pts[-1][0]


def _on_skirt(a_deg, z, out=1.0):
    """A point on the skirt's surface at angle a (0 is +X, -90 the front)
    and height z under the waist, `out` times its radius."""
    a = math.radians(a_deg)
    rr = _bell(SKIRT, z) * out
    return (math.cos(a) * rr, math.sin(a) * rr, z)


# Where the four blades of each hand start along the knuckles, and how far
# each is fanned out from the middle (degrees about the hand's Y).
FINGER_X = (-0.054, -0.018, 0.018, 0.054)
FAN = (20.0, 7.0, -7.0, -20.0)


# =================================================================================
#  The Shear Mannequin
# =================================================================================
def build_shear_mannequin():
    """Wynn's nightmare: the bare dress form in her dream shop, dressed at
    last in the gown of the portrait, and grown nightmare-tall -- and then it
    moves. A turned oak stand on three splayed feet with brass on them; a
    full ivory skirt belled out over it, a crimson bodice laced over the
    padded torso, a gold sash at the waist knotted in a bow with its tails
    down the skirt, pins left in it everywhere glinting; the wooden knob of
    a head on its neck, cracked. Out of the shoulders, where a dress form
    has none, come jointed wooden arms like an artist's lay figure's, and
    each hand has split lengthwise into four long scissor blades -- a dark
    back and a bright edge -- where the fingers were."""
    r = Rig()
    r.joint("base", (0, 0, 0))                       # what spins, hops and topples

    # The stand: three feet off a hub, brass casters, a turned pole up into
    # the skirt.
    r.add("hub", E(0.105, 0.105, 0.075), "sm_oak", "base", loc=(0, 0, 0.13))
    r.add("hub_ring", TORUS(0.080, 0.026), "sm_wood_dk", "base", loc=(0, 0, 0.20))
    for k, a in enumerate((90.0, 210.0, 330.0)):
        c, s = math.cos(math.radians(a)), math.sin(math.radians(a))
        r.limb("leg", (c * 0.05, s * 0.05, 0.15), (c * 0.52, s * 0.52, 0.06), 0.052, "sm_oak", "base", r_tip=0.040)
        r.add("caster", E(0.066, 0.066, 0.052), "sm_brass", "base", loc=(c * 0.545, s * 0.545, 0.052))
    r.limb("pole", (0, 0, 0.14), (0, 0, 1.06), 0.050, "sm_oak", "base", r_tip=0.046)
    r.add("knop", E(0.078, 0.078, 0.050), "sm_wood_dk", "base", loc=(0, 0, 0.31))

    # The skirt, on a joint of its own so it can flare in a spin and puff out
    # when it lands from a hop.
    r.joint("waist", (0, 0, 1.08), "base")
    r.joint("skirt", (0, 0, 0.0), "waist")
    r.add("skirt", lathe(SKIRT), "sm_ivory", "skirt")
    # Folds down the front and sides of it, a shade darker, following the bell.
    # Seven: eleven of them ribbed it like a paper lantern.
    for k in range(7):
        a = -90.0 + (k - 3) * 26.0
        pts = [_on_skirt(a, z, 1.012) for z in (-0.07, -0.30, -0.53, -0.76)]
        for i in range(3):
            r.limb("pleat", pts[i], pts[i + 1], 0.011 + 0.005 * i, "sm_ivory_dk", "skirt", r_tip=0.014 + 0.005 * i)
    r.add("hem", TORUS(0.455, 0.024), "sm_ivory_dk", "skirt", loc=(0, 0, -0.772))
    # Pins still in it where the hem was taken up and a tear was caught.
    for a, z in ((-122.0, -0.56), (-64.0, -0.66), (-34.0, -0.38), (-150.0, -0.34), (165.0, -0.50)):
        _pin(r, "skirt", Vector(_on_skirt(a, z, 1.0)), a)

    # The sash: a band of gold round the waist, a bow on the left hip and its
    # tails down the skirt, which fly out when it spins.
    # Round, and a little wider than the top of the skirt it sits on: the
    # skirt is round where the bodice is flattened, and a band fitted to the
    # bodice was lost behind the skirt's top in front.
    r.joint("sash", (0, 0, 0.0), "waist")
    r.add("sash_band", FRUSTUM(0.218, 0.248, 0.11), "sm_gold", "sash", loc=(0, 0, 0.055))
    r.add("sash_edge", TORUS(0.246, 0.014), "sm_gold_dk", "sash", loc=(0, 0, -0.052))
    bow = Vector((-0.117, -0.225, 0.012))
    for sx in (-1, 1):
        r.add("bow_loop", E(0.080, 0.034, 0.052), "sm_gold", "sash", loc=tuple(bow + Vector((sx * 0.068, -0.012, 0.016))),
              rot=(0, math.radians(sx * -30), math.radians(-28)))
    r.add("bow_knot", E(0.040, 0.034, 0.038), "sm_gold_dk", "sash", loc=tuple(bow + Vector((0, -0.020, 0))))
    # The tails end just off the skirt's surface, where it has belled out to
    # under them.
    r.joint("tails", tuple(bow + Vector((0, -0.01, -0.02))), "sash")
    for k, (end, rr) in enumerate((((-0.038, -0.056, -0.322), 0.034), ((0.039, -0.055, -0.242), 0.030))):
        r.limb("tail", (0, 0, 0), end, rr, "sm_gold", "tails", r_tip=rr * 0.9)
        r.add("tail_end", E(rr * 1.25, rr * 0.6, rr * 0.9), "sm_gold_dk", "tails", loc=end)

    # The bodice over the form's padded torso, puffed caps where the arms come
    # out, and a seam of lacing up the back.
    r.joint("chest", (0, 0, 0.02), "waist")
    bodice = r.add("bodice", lathe(BODICE), "sm_crimson", "chest")
    bodice.scale = (1.0, BODICE_FLAT, 1.0)
    r.add("bust_dk", E(0.20, 0.06, 0.085), "sm_crimson_dk", "chest", loc=(0, -0.155, 0.19))
    for sx in (-1, 1):
        r.add("puff", E(0.120, 0.115, 0.098), "sm_crimson", "chest", loc=(sx * 0.285, 0, 0.405))
    for k in range(4):
        r.add("lace", E(0.026, 0.016, 0.020), "sm_gold", "chest", loc=(0, 0.200 - 0.004 * k, 0.10 + 0.085 * k))
    _pin(r, "chest", Vector((0.13, -_bell(BODICE, 0.30) * BODICE_FLAT * 0.92, 0.30)), -90.0)

    # The neck and the knob it has for a head, cracked, with a pin in it.
    r.joint("neck", (0, 0, 0.47), "chest")
    r.add("collar", E(0.100, 0.090, 0.032), "sm_wood_dk", "neck", loc=(0, 0, 0.004))
    r.limb("neckp", (0, 0, 0.0), (0, 0, 0.135), 0.054, "sm_wood", "neck", r_tip=0.050)
    r.joint("head", (0, 0, 0.14), "neck")
    r.add("knob", E(0.118, 0.115, 0.128), "sm_wood", "head", loc=(0, 0, 0.11))
    r.add("finial", E(0.042, 0.042, 0.046), "sm_wood_dk", "head", loc=(0, 0, 0.250))
    r.add("crack", E(0.013, 0.012, 0.066), "sm_void", "head", loc=(0.036, -0.108, 0.115), rot=(0, 0.42, 0))
    r.add("crack2", E(0.011, 0.012, 0.034), "sm_void", "head", loc=(0.060, -0.098, 0.060), rot=(0, -0.5, 0))
    _pin(r, "head", Vector((-0.07, -0.07, 0.19)), -120.0, length=0.07)

    # The arms: a lay figure's, ball-jointed, coming out under the puffs.
    for sx, side in ((-1, "l"), (1, "r")):
        sh = "shoulder_" + side
        r.joint(sh, (sx * 0.31, 0, 0.40), "chest", rest=(0, sx * -24, 0))
        r.add("sh_ball", E(0.074, 0.074, 0.074), "sm_wood_dk", sh)
        r.limb("upper", (0, 0, -0.04), (0, 0, -0.34), 0.052, "sm_wood", sh, r_tip=0.044)
        r.joint("elbow_" + side, (0, 0, -0.38), sh, rest=(-18, 0, 0))
        r.add("el_ball", E(0.058, 0.058, 0.058), "sm_wood_dk", "elbow_" + side)
        r.limb("fore", (0, 0, -0.03), (0, 0, -0.31), 0.044, "sm_wood", "elbow_" + side, r_tip=0.038)
        hand = "hand_" + side
        r.joint(hand, (0, 0, -0.35), "elbow_" + side)
        r.add("wrist", E(0.048, 0.048, 0.048), "sm_wood_dk", hand)
        r.add("palm", rbox(0.165, 0.070, 0.115, 0.5), "sm_wood", hand, loc=(0, 0, -0.068))
        # Four fingers, each a wooden stub with a blade along it: the stub is
        # all that is left when the blades fold away in the death. Each blade
        # is one long tapering knife of bright steel, three pixels across at
        # the root, with its dark back a line down one side of it, standing
        # proud of both faces so it shows whichever way the hand is turned.
        # A dark half and a bright half side by side, the four of them were
        # eight stripes: a comb.
        for k in range(4):
            fj = "fin_%s%d" % (side, k)
            r.joint(fj, (FINGER_X[k], 0, -0.118), hand, rest=(0, FAN[k], 0))
            r.limb("stub", (0, 0, 0.012), (0, 0, -0.080), 0.026, "sm_wood", fj, r_tip=0.020)
            bj = "bl_%s%d" % (side, k)
            r.joint(bj, (0, 0, -0.012), fj)
            r.add("blade", C(0.037, 0.006, 0.50, squash_y=0.30), "sm_edge", bj, loc=(sx * 0.004, 0, 0))
            r.add("blade_back", C(0.016, 0.007, 0.40, squash_y=1.0), "sm_steel_dk", bj, loc=(-sx * 0.026, 0, -0.01))
            r.add("rivet", E(0.022, 0.026, 0.022), "sm_steel_dk", bj, loc=(0, 0, -0.03))
    return fitted(r, "shear_mannequin", SM_PX, SM_REST)


def _pin(r, parent, at, a_deg, length=0.06):
    """A dressmaker's pin pushed in at `at`, its head standing out a little
    off the surface along the way the surface faces there, with a glint on
    the head."""
    a = math.radians(a_deg)
    out = Vector((math.cos(a), math.sin(a), 0.35)).normalized()
    head = at + out * length
    r.limb("pin", tuple(at - out * 0.01), tuple(head), 0.010, "sm_pin", parent, r_tip=0.008)
    r.add("pin_glint", E(0.030, 0.030, 0.030), "sm_pin_glow", parent, loc=tuple(head))


# How tall it stands on screen, in game pixels: about the Ashen Vanguard's
# and Vexel's height -- two and a half times the hero, and half as tall again
# as the dress form in the waking shop it was.
SM_PX = 68


# --- poses ------------------------------------------------------------------------------
def _fan(spread=1.0, sides="lr", curl=0.0):
    """The blades of each hand fanned `spread` times as wide as at rest -- 0
    is shut together, 2 twice as wide -- and curled toward the palm by
    `curl` degrees."""
    v = {}
    for side in sides:
        for k in range(4):
            v["fin_%s%d" % (side, k)] = (-curl, FAN[k] * (spread - 1.0), 0)
    return v


def _fold(k):
    """The death's blades folding back into the fingers: turned back against
    the hand and drawn in to nothing, k from 0 (out) to 1 (gone)."""
    v = {}
    for side in "lr":
        for j in range(4):
            v["bl_%s%d" % (side, j)] = (110.0 * k, 0, 0)
            v["~bl_%s%d" % (side, j)] = -0.99 * k
    return v


def _hands(lp, la, rp, ra, le=None, re=None):
    """Both hands put somewhere in the chest's space and their blades aimed."""
    v = {"^l": lp, "^l_aim": la, "^r": rp, "^r_aim": ra}
    if le:
        v["^l_edge"] = le
    if re:
        v["^r_edge"] = re
    return v


def _sm(**more):
    v = dict(SM_HANDS)
    v.update(_fan(1.0))
    v.update(more)
    return v


# Ready: the hands held out from the skirt at the hip, the blades fanned down
# and out like claws -- out where they show against the floor in every
# facing rather than lying flat against the ivory.
SM_LH, SM_LA = (-0.56, -0.30, -0.08), (-0.30, -0.45, -0.84)
SM_RH, SM_RA = (0.56, -0.30, -0.08), (0.30, -0.45, -0.84)
SM_HANDS = _hands(SM_LH, SM_LA, SM_RH, SM_RA)
SM_REST = dict(SM_HANDS)


def sm_idle(t):
    # It stands on its feet the way nothing alive does: a slow sway on the
    # pole, the knob of a head tipping, and the blades scissoring a little
    # open and shut, never still.
    s, c = sn(t), sn(t, 0.25)
    v = _sm(_z=0.006 + 0.006 * s, chest=(2 * s, 0, 4 * c), head=(4 * c, 0, 6 * s), skirt=(0, 0, -2 * c),
            tails=(4 * s, 0, 0))
    v.update(_hands((SM_LH[0], SM_LH[1], SM_LH[2] + 0.025 * s), SM_LA,
                    (SM_RH[0], SM_RH[1], SM_RH[2] - 0.025 * s), SM_RA))
    v.update(_fan(1.0 + 0.30 * s, "l"))
    v.update(_fan(1.0 - 0.30 * s, "r"))
    return v


def sm_walk(t):
    # It hops: down onto its feet with the skirt puffed out round it, up off
    # them with the skirt stretched after it, a lean into the way it goes and
    # the blades swinging back and forward.
    up = math.sin(t * math.pi)                       # one hop a cycle
    land = 1.0 - up
    v = _sm(_z=0.15 * up ** 1.3, chest=(8 + 4 * up, 0, 0), head=(-6 * up, 0, 0),
            tails=(-14 * up, 0, 0))
    v["%skirt"] = (1.0 + 0.07 * land - 0.03 * up, 1.0 + 0.07 * land - 0.03 * up, 1.0 - 0.06 * land + 0.05 * up)
    v["_squash"] = (1.0 + 0.04 * land, 1.0 + 0.04 * land, 1.0 - 0.05 * land + 0.03 * up)
    sw = 0.10 * math.cos(t * math.tau)
    v.update(_hands((SM_LH[0], SM_LH[1] + sw, SM_LH[2] + 0.10 * up), (SM_LA[0], SM_LA[1] + sw, SM_LA[2]),
                    (SM_RH[0], SM_RH[1] + sw, SM_RH[2] + 0.10 * up), (SM_RA[0], SM_RA[1] + sw, SM_RA[2])))
    v.update(_fan(1.0 + 0.4 * up))
    return v


# The slash: the right hand drawn up and back over its shoulder with the
# blades spread, brought across in front of it low to the left, and back.
# Up and across rather than at the camera: from forty-six degrees overhead a
# cut toward the viewer hardly moves.
SM_SLASH = [
    None,
    dict(_hands((-0.60, -0.14, -0.06), (-0.40, -0.30, -0.87), (0.56, 0.08, 0.80), (0.45, 0.25, 0.86)),
         chest=(-4, 0, -18), head=(0, 0, 8), **_fan(1.7, "r")),
    dict(_hands((-0.62, -0.10, 0.10), (-0.50, -0.30, -0.80), (0.50, 0.12, 0.92), (0.30, 0.35, 0.89)),
         chest=(-8, 0, -24), head=(0, 0, 10), **_fan(1.9, "r")),
    dict(_hands((-0.66, -0.06, 0.12), (-0.60, -0.20, -0.77), (-0.30, -0.78, 0.10), (-0.78, -0.50, -0.36)),
         chest=(14, 0, 26), head=(0, 0, -10), _y=-0.06, **_fan(1.3, "r")),
    dict(_hands((-0.66, -0.06, 0.12), (-0.60, -0.20, -0.77), (-0.62, -0.52, -0.06), (-0.92, -0.18, -0.36)),
         chest=(16, 0, 34), head=(0, 0, -12), _y=-0.07, **_fan(1.1, "r")),
    dict(_hands((-0.62, -0.12, 0.08), (-0.52, -0.38, -0.76), (0.10, -0.62, 0.02), (-0.20, -0.75, -0.62)),
         chest=(8, 0, 12), _y=-0.03, **_fan(1.0, "r")),
]


def sm_attack(t):
    frame = min(5, int(round(t * 5)))
    if frame == 0:
        return sm_idle(0.0)
    v = _sm()
    v.update(SM_SLASH[frame])
    return v


def sm_hurt(t):
    # Knocked back on its stand: the body thrown back off the pole, the knob
    # jolted round, the arms flung up and the blades sprung wide, the skirt
    # shaken out.
    k = math.sin(t * math.pi)
    v = _sm(chest=(-18 * k, 0, 8 * k), head=(-16 * k, 0, 22 * k), _y=0.07 * k, skirt=(-6 * k, 0, 0),
            tails=(-20 * k, 0, 0))
    v.update(_hands((-0.66, 0.00, 0.06 + 0.40 * k), (-0.50, -0.10, -0.86 + 1.2 * k),
                    (0.66, 0.00, 0.06 + 0.36 * k), (0.50, -0.10, -0.86 + 1.1 * k)))
    v.update(_fan(1.0 + 0.9 * k))
    v["%skirt"] = (1.0 + 0.05 * k, 1.0 + 0.05 * k, 1.0 - 0.04 * k)
    return v


def sm_death(t):
    """The blades fold back into wooden fingers, the arms drop, the dress
    slumps on the form, and the whole of it goes over sideways onto the
    floor -- a dress form knocked over, with a gown on it."""
    frame = min(5, int(round(t * 5)))
    if frame == 0:
        v = _sm(chest=(-14, 0, -6), head=(-14, 0, -18), _y=0.05)
        v.update(_hands((-0.64, 0.02, 0.36), (-0.40, 0.20, 0.89), (0.64, 0.02, 0.32), (0.40, 0.20, 0.89)))
        v.update(_fan(1.8))
        return v
    fold = (0.0, 0.55, 1.0, 1.0, 1.0, 1.0)[frame]
    slump = (0.0, 0.40, 0.85, 1.0, 1.0, 1.0)[frame]
    over = (0.0, 0.0, 0.0, 0.40, 0.80, 1.0)[frame]
    v = {"chest": (24 * slump, 0, -6 * slump), "head": (26 * slump, 0, 18 * slump), "neck": X(10 * slump),
         "tails": (12 * slump, 0, 0), "skirt": (6 * slump, 0, 0)}
    # The arms hang dead from the shoulders, a little out, then go where the
    # fall throws them.
    v.update({"shoulder_l": (-10 * slump + 30 * over, 0, 0), "shoulder_r": (-10 * slump + 20 * over, 0, 0),
              "elbow_l": X(-20 * slump), "elbow_r": X(-30 * slump)})
    v.update(_fan(1.0 - 0.6 * fold, curl=30 * fold))
    v.update(_fold(fold))
    # The skirt sags off the form: lower and wider, as cloth with nothing in
    # it does.
    v["%skirt"] = (1.0 + 0.10 * slump, 1.0 + 0.10 * slump, 1.0 - 0.16 * slump)
    v["%chest"] = (1.0, 1.0, 1.0 - 0.06 * slump)
    if over > 0.0:
        v.update({"_wroll": 86.0 * ease(over), "_wz": 0.24 * ease(over), "_z": -0.04 * over})
    return v


def sm_spin(t):
    """The spin cycle: lifted a hand's breadth off its feet, arms flung out
    straight with the blades swept back against the turn like a pinwheel's,
    the skirt flared out flat round it and the sash's tails flying -- and the
    whole of it turning, a full turn every eight frames, so it loops."""
    v = _sm(chest=X(4), head=(-10, 0, 0), _z=0.10, tails=(-70, 0, 0), skirt=(0, 0, 0))
    v.update(_hands((-1.02, -0.06, 0.46), (-0.86, -0.48, 0.02), (1.02, 0.06, 0.46), (0.86, 0.48, 0.02)))
    v.update(_fan(0.75))
    v["%skirt"] = (1.24, 1.24, 0.80)
    v["base"] = (0, 0, -360.0 * t)
    return v


# The snip: its arms come up and out to either side of whatever is in front
# of it with the blades fanned wide open, held there a beat -- three frames
# of a trembling, gathering pull -- and snapped shut across each other in
# front of it on the sixth frame (index 5), held shut, and drawn back.
# Open low and forward, not level at the shoulder: facing the camera, arms
# spread level were only arms spread, and the space they close on -- what is
# in front of it, drawn under it on screen -- was outside them. Shut, the
# blades are driven down past the hem, where they cross against the floor
# and not against the ivory.
SNIP_SHUT = 5
SM_SNIP = [
    None,
    (_hands((-0.70, -0.36, 0.30), (-0.55, -0.80, 0.20), (0.70, -0.36, 0.30), (0.55, -0.80, 0.20)),
     dict(chest=X(-8), head=X(-10)), 1.8),
    (_hands((-0.86, -0.56, 0.12), (-0.30, -0.93, -0.20), (0.86, -0.56, 0.12), (0.30, -0.93, -0.20)),
     dict(chest=X(2), head=X(-4), _y=-0.03), 2.6),
    (_hands((-0.88, -0.54, 0.11), (-0.28, -0.94, -0.19), (0.88, -0.54, 0.13), (0.28, -0.94, -0.18)),
     dict(chest=(4, 0, 2), head=(-4, 0, 3), _y=-0.04), 2.8),
    (_hands((-0.90, -0.48, 0.14), (-0.24, -0.95, -0.18), (0.90, -0.48, 0.14), (0.24, -0.95, -0.18)),
     dict(chest=X(6), head=(-2, 0, -3), _y=-0.05), 2.7),
    # Shut: the arms thrown together in front and every blade drawn in to
    # one point, as a pair of shears' blades meet. Crossed at the wrists,
    # the blades went on outward from each other: a V, and a V is open.
    (_hands((-0.06, -0.80, 0.00), (0.16, -0.82, -0.55), (0.06, -0.78, 0.02), (-0.16, -0.82, -0.55)),
     dict(chest=X(16), head=X(8), _y=-0.11), -0.3),
    (_hands((-0.06, -0.78, 0.02), (0.16, -0.82, -0.55), (0.06, -0.76, 0.04), (-0.16, -0.82, -0.55)),
     dict(chest=X(12), head=X(6), _y=-0.09, _z=0.015), -0.3),
    (_hands((-0.45, -0.45, -0.05), (-0.30, -0.60, -0.74), (0.45, -0.45, -0.03), (0.30, -0.60, -0.74)),
     dict(chest=X(6), _y=-0.04), 0.6),
]


def sm_snip(t):
    frame = min(7, int(round(t * 7)))
    if frame == 0:
        return sm_idle(0.0)
    hands, body, spread = SM_SNIP[frame]
    v = _sm()
    v.update(hands)
    v.update(body)
    v.update(_fan(spread))
    return v


# =================================================================================
#  Registration
# =================================================================================

# id: (builder, frame px, (idle, walk, attack, hurt, death), shadow radius)
CREATURES = {
    # A hundred and sixty: its arms flung out straight in the spin, blades and
    # all, reach most of the way across it.
    "shear_mannequin": (build_shear_mannequin, 160, (sm_idle, sm_walk, sm_attack, sm_hurt, sm_death), 1.00),
}

# id: {clip: (pose, frames, loops)} -- see EXTRA_CLIPS in blender_creatures.py.
# The frame rates are make_sprites_json.ps1's ($spriteClipRules).
EXTRA = {
    "shear_mannequin": {"spin": (sm_spin, 8, True), "snip": (sm_snip, 8, False)},
}


def register():
    cr.CREATURES.update(CREATURES)
    for name, clips in EXTRA.items():
        cr.EXTRA_CLIPS.setdefault(name, {}).update(clips)
