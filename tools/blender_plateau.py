# =============================================================================
#  blender_plateau.py - what lives on Purgatory's Plateau: the five elemental
#  dragons, the Greater Demon, and Cerberus, who walks round the Stronghold.
#
#  Rendered by tools/make_creatures.ps1 like every other monster:
#      .\tools\make_creatures.ps1 -Only dragon_fire,cerberus
#
#  Built out of the same parts as blender_creatures.py and fitted to a height
#  on screen the way blender_bestiary.py's monsters are (its Rig); registered
#  into the same CREATURES table, so a Storm Dragon's sheet is made exactly the
#  way a rat's is. blender_creatures.py hands itself over at its foot.
#
#  The dragons are one dragon -- Hoarfang's frame, heavier or lighter -- dressed
#  five ways. What tells them apart at forty pixels is never only the colour:
#  the fire one is cracked and glowing along its spines, the water one has fins
#  where the others have spikes, the earth one is carrying rock on its back, the
#  air one is feathered and long-tailed, and the lightning one has a bolt drawn
#  down each flank. They move with Hoarfang's clips.
# =============================================================================

import math

import blender_character as bc
import blender_creatures as cr
import blender_bestiary as bb

E, C = cr.E, cr.C
X, fwd, sn, ease, phases, mix = cr.X, cr.fwd, cr.sn, cr.ease, cr.phases, cr.mix
gait, topple = cr.gait, cr.topple
humanoid_legs, humanoid_arms = cr.humanoid_legs, cr.humanoid_arms
Rig = bb.Rig


# --- colours ---------------------------------------------------------------------------
bc.PALETTE.update({
    # Pyre Dragon: charred crimson, split along its seams to the fire inside
    "pyre": (0.44, 0.10, 0.08), "pyre_dk": (0.22, 0.06, 0.05), "pyre_belly": (0.86, 0.46, 0.16),
    "pyre_horn": (0.14, 0.10, 0.09), "pyre_wing": (0.58, 0.16, 0.10), "pyre_glow": (1.00, 0.56, 0.14),
    "pyre_eye_glow": (1.00, 0.88, 0.36),
    # Brine Dragon: sea-green, finned, pearl-horned
    "brine": (0.14, 0.42, 0.46), "brine_dk": (0.08, 0.26, 0.31), "brine_belly": (0.74, 0.88, 0.80),
    "brine_horn": (0.88, 0.90, 0.84), "brine_wing": (0.26, 0.60, 0.64), "brine_glow": (0.52, 0.96, 1.00),
    "brine_eye_glow": (0.66, 1.00, 0.92),
    # Basalt Dragon: grey-brown stone, rock on its back, amber in the cracks
    "basalt": (0.38, 0.33, 0.29), "basalt_dk": (0.23, 0.20, 0.18), "basalt_belly": (0.60, 0.50, 0.36),
    "basalt_horn": (0.17, 0.15, 0.14), "basalt_wing": (0.42, 0.32, 0.24), "basalt_rock": (0.50, 0.47, 0.44),
    "basalt_rock_dk": (0.32, 0.30, 0.29), "basalt_glow": (1.00, 0.70, 0.24), "basalt_eye_glow": (1.00, 0.78, 0.30),
    # Gale Dragon: pale as the sky over the flats, feathered
    "gale": (0.76, 0.83, 0.90), "gale_dk": (0.54, 0.62, 0.73), "gale_belly": (0.95, 0.96, 0.97),
    "gale_horn": (0.60, 0.64, 0.71), "gale_wing": (0.70, 0.80, 0.93), "gale_feather": (0.97, 0.98, 1.00),
    "gale_glow": (0.84, 0.97, 1.00), "gale_eye_glow": (0.42, 0.82, 1.00),
    # Storm Dragon: the dark of a thunderhead, the bolt down its sides
    "storm": (0.17, 0.18, 0.27), "storm_dk": (0.09, 0.09, 0.15), "storm_belly": (0.40, 0.42, 0.54),
    "storm_horn": (0.90, 0.80, 0.40), "storm_wing": (0.24, 0.26, 0.40), "storm_glow": (1.00, 0.96, 0.46),
    "storm_eye_glow": (1.00, 1.00, 0.70),
    # The Greater Demon: blood-red under obsidian plate, a mane of fire
    "gdemon": (0.44, 0.09, 0.07), "gdemon_dk": (0.24, 0.05, 0.05), "gdemon_armour": (0.15, 0.13, 0.16),
    "gdemon_armour_lt": (0.30, 0.27, 0.31), "gdemon_gold": (0.76, 0.56, 0.22), "gdemon_horn": (0.20, 0.17, 0.16),
    "gdemon_wing": (0.30, 0.07, 0.07), "gdemon_glow": (1.00, 0.50, 0.12), "gdemon_eye_glow": (1.00, 0.86, 0.34),
    "gdemon_blade": (0.20, 0.18, 0.20), "gdemon_edge": (0.62, 0.60, 0.58),
    # Cerberus: soot-black, three heads, iron collars, fire in the throat
    "cerb": (0.27, 0.22, 0.20), "cerb_dk": (0.15, 0.11, 0.10), "cerb_belly": (0.38, 0.30, 0.26),
    "cerb_muzzle": (0.48, 0.40, 0.35), "cerb_collar": (0.24, 0.24, 0.27), "cerb_collar_lt": (0.50, 0.50, 0.53),
    "cerb_snake": (0.30, 0.36, 0.22),
    "cerb_snake_dk": (0.19, 0.24, 0.14), "cerb_glow": (1.00, 0.46, 0.12), "cerb_eye_glow": (1.00, 0.86, 0.30),
})


# =================================================================================
#  The elemental dragons
# =================================================================================

def _palette(k):
    return {"body": k, "dk": k + "_dk", "belly": k + "_belly", "horn": k + "_horn",
            "wing": k + "_wing", "glow": k + "_glow", "eye": k + "_eye_glow"}


def make_dragon(p, heft=1.0, slim=1.0, sail=1.0, ridge=True, barbs=True):
    """Hoarfang's frame, in a dragon's own colours. `heft` thickens the legs and
    the body, `slim` narrows it, `sail` sizes the wings; `ridge` and `barbs`
    are the spikes down the spine and on the tail, which not every dragon has."""
    r = Rig()
    r.joint("body", (0, 0.10, 1.10), rest=(-6, 0, 0))
    w = slim * heft
    r.add("torso", E(0.56 * w, 0.92, 0.52 * heft), p["body"], "body")
    r.add("haunch", E(0.52 * w, 0.40, 0.44 * heft), p["body"], "body", loc=(0, 0.46, -0.04))
    r.add("belly", E(0.42 * w, 0.78, 0.28 * heft), p["belly"], "body", loc=(0, -0.02, -0.26))
    r.add("chest", E(0.48 * w, 0.38, 0.44 * heft), p["body"], "body", loc=(0, -0.52, -0.04))
    if ridge:
        for k in range(7):
            y = -0.56 + k * 0.19
            h = 0.20 + 0.12 * math.cos((k - 2) * 0.9)
            r.limb("ridge", (0, y, 0.40), (0, y - 0.05, 0.40 + h), 0.07, p["horn"], "body", r_tip=0.006)

    for s2, side in ((-1, "l"), (1, "r")):
        for tag, hip_y, thigh, shin, rr in (("fore", -0.46, -0.40, -0.36, 0.15), ("hind", 0.40, -0.46, -0.42, 0.19)):
            j = tag + "_" + side
            rr *= heft
            r.joint(j, (s2 * 0.38 * w, hip_y, -0.18), "body", rest=(10 if tag == "hind" else 6, 0, 0))
            r.limb("thigh", (0, 0, 0), (0, -0.10, thigh), rr, p["body"], j, r_tip=rr * 0.72)
            r.joint(j + "_knee", (0, -0.10, thigh), j, rest=(0, 0, 0))
            r.limb("shin", (0, 0, 0), (0, 0.08, shin), rr * 0.66, p["dk"], j + "_knee", r_tip=rr * 0.52)
            r.add("foot", E(0.14 * heft, 0.18 * heft, 0.07), p["dk"], j + "_knee", loc=(0, 0.02, shin))
            for k in (-1, 0, 1):
                r.limb("claw", (0, 0.06, shin), (k * 0.13, -0.20, shin - 0.05), 0.045, p["horn"],
                       j + "_knee", r_tip=0.008)

    r.joint("neck1", (0, -0.70, 0.20), "body", rest=(46, 0, 0))
    r.limb("neck", (0, 0, 0), (0, 0, 0.44), 0.22 * heft, p["body"], "neck1", r_tip=0.18 * heft)
    r.joint("neck2", (0, 0, 0.44), "neck1", rest=(-34, 0, 0))
    r.limb("neck", (0, 0, 0), (0, 0, 0.36), 0.18 * heft, p["body"], "neck2", r_tip=0.15 * heft)
    r.joint("head", (0, 0, 0.36), "neck2", rest=(-14, 0, 0))
    r.add("skull", E(0.25, 0.34, 0.22), p["body"], "head", loc=(0, -0.14, 0.02))
    r.add("snout", E(0.15, 0.30, 0.13), p["body"], "head", loc=(0, -0.40, -0.02))
    r.add("jaw", E(0.13, 0.28, 0.08), p["belly"], "head", loc=(0, -0.38, -0.12))
    r.add("breath", E(0.09, 0.09, 0.08), p["glow"], "head", loc=(0, -0.58, -0.04))
    for s2 in (-1, 1):
        r.add("eye", E(0.05, 0.05, 0.045), p["eye"], "head", loc=(s2 * 0.14, -0.26, 0.11))
        r.limb("fang", (s2 * 0.08, -0.40, -0.08), (s2 * 0.09, -0.42, -0.20), 0.026, p["horn"], "head", r_tip=0.004)

    r.joint("tail1", (0, 0.84, 0.02), "body", rest=(74, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.58), 0.22 * heft, p["body"], "tail1", r_tip=0.16 * heft)
    r.joint("tail2", (0, 0, -0.58), "tail1", rest=(10, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.56), 0.16 * heft, p["body"], "tail2", r_tip=0.10 * heft)
    r.joint("tail3", (0, 0, -0.56), "tail2", rest=(10, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.42), 0.12 * heft, p["dk"], "tail3", r_tip=0.04)
    if barbs:
        for k in (-1, 1):
            r.limb("barb", (0, 0, -0.34), (k * 0.20, 0, -0.56), 0.05, p["horn"], "tail3", r_tip=0.006)
            r.limb("barb", (0, 0, -0.46), (k * 0.13, 0, -0.66), 0.04, p["horn"], "tail3", r_tip=0.005)

    for s2, side in ((-1, "l"), (1, "r")):
        r.joint("wing_" + side, (s2 * 0.34 * w, -0.34, 0.34), "body", rest=(0, 0, 0))
        r.limb("arm", (0, 0, 0), (s2 * 0.24, 0.18, 0.36), 0.09, p["dk"], "wing_" + side, r_tip=0.06)
        r.joint("wrist_" + side, (s2 * 0.24, 0.18, 0.36), "wing_" + side, rest=(0, 0, 0))
        f = sail
        r.limb("finger", (0, 0, 0), (s2 * 0.10 * f, 0.96 * f, -0.34 * f), 0.05, p["dk"], "wrist_" + side, r_tip=0.010)
        r.limb("finger", (0, 0, 0), (s2 * 0.30 * f, 0.74 * f, -0.40 * f), 0.042, p["dk"], "wrist_" + side, r_tip=0.009)
        r.limb("finger", (0, 0, 0), (s2 * 0.48 * f, 0.32 * f, -0.42 * f), 0.036, p["dk"], "wrist_" + side, r_tip=0.008)
        r.add("sail1", E(0.05, 0.46 * f, 0.34 * f), p["wing"], "wrist_" + side, loc=(s2 * 0.16 * f, 0.48 * f, -0.34 * f),
              rot=(0, s2 * 0.18, 0))
        r.add("sail2", E(0.05, 0.34 * f, 0.28 * f), p["wing"], "wrist_" + side, loc=(s2 * 0.32 * f, 0.46 * f, -0.40 * f),
              rot=(0, s2 * 0.30, 0))
        r.limb("thumb", (0, 0, 0), (s2 * 0.18, -0.12, 0.20), 0.035, p["horn"], "wrist_" + side, r_tip=0.005)
    return r


def _horns(r, p, sweep=1.0, fork=False, colour=None):
    c = colour or p["horn"]
    for s2 in (-1, 1):
        r.limb("horn", (s2 * 0.11, 0.06, 0.14), (s2 * 0.28, 0.54 * sweep, 0.38), 0.08, c, "head", r_tip=0.008)
        if fork:
            r.limb("tine", (s2 * 0.20, 0.30 * sweep, 0.26), (s2 * 0.34, 0.26 * sweep, 0.52), 0.045, c, "head",
                   r_tip=0.006)


def build_dragon_fire():
    """The Pyre Dragon: split along its sides to the fire inside it, the spines
    down its back tipped with it, smoke-black horns swept well back."""
    p = _palette("pyre")
    r = make_dragon(p)
    _horns(r, p, sweep=1.15)
    for s2 in (-1, 1):
        for k, (y, z, rot) in enumerate(((-0.40, 0.10, 0.5), (-0.06, 0.18, -0.4), (0.26, 0.06, 0.6), (0.52, 0.16, -0.3))):
            r.add("seam", E(0.025, 0.16, 0.022), p["glow"], "body", loc=(s2 * 0.53, y, z), rot=(rot, 0, 0))
        for k in range(3):
            r.limb("frill", (s2 * 0.15, 0.10 + k * 0.05, 0.02), (s2 * 0.34, 0.26 + k * 0.08, -0.06 - k * 0.06),
                   0.04, p["dk"], "head", r_tip=0.004)
    for k in range(7):
        y = -0.56 + k * 0.19
        h = 0.20 + 0.12 * math.cos((k - 2) * 0.9)
        r.add("flame", E(0.05, 0.05, 0.07), p["glow"], "body", loc=(0, y - 0.05, 0.40 + h))
    r.add("belly_glow", E(0.16, 0.50, 0.04), p["glow"], "body", loc=(0, -0.04, -0.52))
    return r.fit("dragon_fire", 70)


def build_dragon_water():
    """The Brine Dragon: fins down its back where the others have spikes, a fin
    either side of the head, trailing barbels, and pearl in its horns."""
    p = _palette("brine")
    r = make_dragon(p, slim=0.94, ridge=False, barbs=False)
    _horns(r, p, sweep=0.9)
    for k in range(6):
        y = -0.54 + k * 0.22
        h = 0.20 + 0.10 * math.cos((k - 2) * 0.8)
        r.add("fin", E(0.025, 0.16, h), p["wing"], "body", loc=(0, y, 0.44 + h * 0.6), rot=(-0.3, 0, 0))
    for s2 in (-1, 1):
        r.add("headfin", E(0.02, 0.16, 0.12), p["wing"], "head", loc=(s2 * 0.24, 0.06, 0.02), rot=(0.2, 0, s2 * 0.7))
        r.limb("barbel", (s2 * 0.08, -0.52, -0.06), (s2 * 0.18, -0.64, -0.32), 0.022, p["belly"], "head", r_tip=0.006)
        for k in range(3):
            r.add("pearl", E(0.03, 0.03, 0.03), p["glow"], "body", loc=(s2 * 0.50, -0.30 + k * 0.30, -0.08))
    r.add("tailfin", E(0.02, 0.22, 0.26), p["wing"], "tail3", loc=(0, 0, -0.46))
    return r.fit("dragon_water", 68)


def build_dragon_earth():
    """The Basalt Dragon: the heaviest of them, carrying slabs of rock on its
    back and shoulders, amber showing in the cracks between, and stubby wings
    it hardly needs."""
    p = _palette("basalt")
    r = make_dragon(p, heft=1.22, sail=0.72, ridge=False)
    _horns(r, p, sweep=0.7)
    slabs = ((-0.52, 0.40, 0.3), (-0.26, 0.46, -0.2), (0.02, 0.46, 0.25), (0.28, 0.44, -0.3), (0.52, 0.36, 0.2))
    for k, (y, z, tilt) in enumerate(slabs):
        r.add("slab", E(0.26, 0.17, 0.10), "basalt_rock" if k % 2 else "basalt_rock_dk", "body",
              loc=(0, y, z + 0.10), rot=(tilt, 0, 0.3 * (1 if k % 2 else -1)))
        r.limb("crystal", (0.10 * (1 if k % 2 else -1), y, z + 0.14), (0.16 * (1 if k % 2 else -1), y - 0.04, z + 0.34),
               0.05, p["glow"], "body", r_tip=0.012)
    for s2 in (-1, 1):
        r.add("boulder", E(0.14, 0.14, 0.12), "basalt_rock", "body", loc=(s2 * 0.46, -0.52, 0.26))
        r.add("crack", E(0.02, 0.10, 0.02), p["glow"], "body", loc=(s2 * 0.62, -0.10, 0.02), rot=(0.4, 0, 0))
    return r.fit("dragon_earth", 70)


def build_dragon_air():
    """The Gale Dragon: the lightest of them, pale, feathered along the backs
    of its wings, with streamers off its head and a fan of feathers for a tail."""
    p = _palette("gale")
    r = make_dragon(p, slim=0.84, sail=1.12, barbs=False)
    _horns(r, p, sweep=1.25)
    for s2, side in ((-1, "l"), (1, "r")):
        for k in range(6):
            t = k / 5.0
            r.limb("feather", (s2 * (0.10 + 0.38 * t) * 1.12, (0.96 - 0.64 * t) * 1.12, -0.36 * 1.12),
                   (s2 * (0.16 + 0.42 * t) * 1.12, (1.10 - 0.62 * t) * 1.12, -0.58 * 1.12), 0.035, "gale_feather",
                   "wrist_" + side, r_tip=0.010)
        r.limb("streamer", (s2 * 0.10, 0.10, 0.10), (s2 * 0.20, 0.70, 0.02), 0.028, p["glow"], "head", r_tip=0.006)
    for k in (-1, 0, 1):
        r.limb("fan", (0, 0, -0.36), (k * 0.22, 0, -0.66), 0.05, "gale_feather", "tail3", r_tip=0.02)
    return r.fit("dragon_air", 68)


def build_dragon_lightning():
    """The Storm Dragon: thunderhead-dark, with a bolt drawn down each flank and
    along each wing, gold horns that fork, and spines that carry the charge."""
    p = _palette("storm")
    r = make_dragon(p)
    _horns(r, p, sweep=1.0, fork=True)
    zig = ((-0.62, 0.20), (-0.40, -0.02), (-0.18, 0.16), (0.06, -0.06), (0.30, 0.12), (0.54, -0.04))
    for s2 in (-1, 1):
        for (y0, z0), (y1, z1) in zip(zig, zig[1:]):
            r.limb("bolt", (s2 * 0.54, y0, z0), (s2 * 0.54, y1, z1), 0.026, p["glow"], "body", r_tip=0.020)
    for s2, side in ((-1, "l"), (1, "r")):
        r.limb("bolt", (s2 * 0.06, 0.30, -0.10), (s2 * 0.20, 0.56, -0.32), 0.022, p["glow"], "wrist_" + side, r_tip=0.014)
        r.limb("bolt", (s2 * 0.20, 0.56, -0.32), (s2 * 0.26, 0.72, -0.22), 0.020, p["glow"], "wrist_" + side, r_tip=0.010)
    for k in range(7):
        y = -0.56 + k * 0.19
        h = 0.20 + 0.12 * math.cos((k - 2) * 0.9)
        r.add("charge", E(0.04, 0.04, 0.05), p["glow"], "body", loc=(0, y - 0.05, 0.40 + h))
    return r.fit("dragon_lightning", 72)


# =================================================================================
#  The Greater Demon
# =================================================================================

def build_greater_demon():
    """The Demon grown to what the Demon is afraid of: half again the height,
    blood-red under obsidian plate trimmed in gold, four horns -- two curled
    back, two forward -- a mane of fire down the back of its skull, wings it
    could cover a cart with, and a cleaver the size of a door in one hand."""
    r = Rig()
    r.joint("pelvis", (0, 0, 0.80))
    r.add("hips", E(0.28, 0.20, 0.17), "gdemon_dk", "pelvis")
    r.add("fauld", C(0.30, 0.34, 0.22, squash_y=0.75), "gdemon_armour", "pelvis", loc=(0, 0, -0.02))
    r.add("belt", E(0.31, 0.22, 0.05), "gdemon_gold", "pelvis", loc=(0, 0, 0.08))
    humanoid_legs(r, -0.05, 0.17, 0.36, 0.40, 0.13, "gdemon", "gdemon_dk", digitigrade=True, foot_col="gdemon_horn")
    for side in ("l", "r"):
        r.add("greave", E(0.10, 0.12, 0.16), "gdemon_armour", "knee_" + side, loc=(0, -0.04, -0.16))
    r.joint("chest", (0, 0, 0.10), "pelvis", rest=(10, 0, 0))
    r.add("torso", E(0.40, 0.27, 0.40), "gdemon", "chest", loc=(0, 0, 0.34))
    r.add("breastplate", E(0.34, 0.20, 0.26), "gdemon_armour", "chest", loc=(0, -0.10, 0.40))
    r.add("trim", E(0.30, 0.18, 0.03), "gdemon_gold", "chest", loc=(0, -0.12, 0.58))
    r.add("core", E(0.07, 0.04, 0.09), "gdemon_glow", "chest", loc=(0, -0.29, 0.38))
    humanoid_arms(r, 0.60, 0.42, 0.38, 0.38, 0.12, "gdemon", "gdemon_dk", flare=18)
    for side in ("l", "r"):
        r.add("pauldron", E(0.20, 0.18, 0.13), "gdemon_armour", "shoulder_" + side, loc=(0, 0, 0.04))
        r.add("rim", E(0.20, 0.18, 0.03), "gdemon_gold", "shoulder_" + side, loc=(0, 0, -0.03))
        r.limb("spike", (0, 0, 0.12), (0, 0.04, 0.30), 0.05, "gdemon_horn", "shoulder_" + side, r_tip=0.008)
        r.add("bracer", E(0.09, 0.09, 0.12), "gdemon_armour", "elbow_" + side, loc=(0, 0, -0.18))
    for k in range(3):
        r.limb("claw", (0, -0.04, -0.06), ((k - 1) * 0.06, -0.12, -0.22), 0.028, "gdemon_horn", "hand_l",
               r_tip=0.005)
    # The cleaver, in the right hand, edge down.
    # Carried high, its edge forward: hanging lower, it swung below the feet
    # in the walk and the sheet's rows stood at different heights.
    r.limb("haft", (0, 0, 0.12), (0, 0, -0.16), 0.035, "gdemon_horn", "hand_r", r_tip=0.03)
    r.add("cleaver", E(0.035, 0.30, 0.18), "gdemon_blade", "hand_r", loc=(0, -0.22, -0.20))
    r.add("edge", E(0.036, 0.04, 0.18), "gdemon_edge", "hand_r", loc=(0, -0.50, -0.20))
    for s, side in ((-1, "l"), (1, "r")):
        r.joint("wing_" + side, (s * 0.20, 0.22, 0.60), "chest", rest=(0, s * 20, s * 30))
        r.limb("wbone", (0, 0, 0), (s * 0.72, 0.14, 0.56), 0.05, "gdemon_horn", "wing_" + side, r_tip=0.02)
        r.limb("wbone", (s * 0.72, 0.14, 0.56), (s * 0.84, 0.30, 0.06), 0.03, "gdemon_horn", "wing_" + side,
               r_tip=0.01)
        r.add("membrane", E(0.40, 0.02, 0.34), "gdemon_wing", "wing_" + side, loc=(s * 0.42, 0.14, 0.18),
              rot=(0, s * -0.6, 0))
    r.joint("neck", (0, -0.04, 0.70), "chest")
    r.joint("head", (0, -0.02, 0.06), "neck", rest=(-12, 0, 0))
    r.add("skull", E(0.17, 0.17, 0.17), "gdemon", "head", loc=(0, 0, 0.11))
    r.add("brow", E(0.16, 0.08, 0.05), "gdemon_dk", "head", loc=(0, -0.12, 0.18))
    r.add("jaw", E(0.14, 0.12, 0.08), "gdemon_dk", "head", loc=(0, -0.09, -0.01))
    r.add("maw", E(0.08, 0.03, 0.02), "gdemon_glow", "head", loc=(0, -0.17, 0.02))
    for s in (-1, 1):
        r.add("eye", E(0.045, 0.03, 0.026), "gdemon_eye_glow", "head", loc=(s * 0.08, -0.15, 0.13))
        # Curled back, and forward over the brow.
        r.limb("horn", (s * 0.12, 0.02, 0.22), (s * 0.34, 0.14, 0.30), 0.07, "gdemon_horn", "head", r_tip=0.05)
        r.limb("horn", (s * 0.34, 0.14, 0.30), (s * 0.38, 0.30, 0.10), 0.05, "gdemon_horn", "head", r_tip=0.012)
        r.limb("brow_horn", (s * 0.08, -0.08, 0.26), (s * 0.16, -0.26, 0.44), 0.04, "gdemon_horn", "head",
               r_tip=0.006)
        r.limb("tusk", (s * 0.07, -0.14, -0.04), (s * 0.09, -0.19, 0.08), 0.02, "tooth", "head", r_tip=0.004)
    for k in range(5):
        r.add("mane", E(0.07, 0.06, 0.10), "gdemon_glow", "head", loc=((k - 2) * 0.05, 0.12 + abs(k - 2) * 0.02,
                                                                           0.18 - abs(k - 2) * 0.05))
    for k in range(3):
        r.add("mane", E(0.08, 0.06, 0.08), "gdemon_glow", "chest", loc=((k - 1) * 0.08, 0.16, 0.66))
    r.joint("tail1", (0, 0.16, -0.04), "pelvis", rest=(84, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.50), 0.06, "gdemon_dk", "tail1", r_tip=0.04)
    r.joint("tail2", (0, 0, -0.50), "tail1", rest=(-50, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.44), 0.04, "gdemon_dk", "tail2", r_tip=0.02)
    r.add("spade", E(0.09, 0.02, 0.10), "gdemon_glow", "tail2", loc=(0, 0, -0.48))
    return r.fit("greater_demon", 84)


def gdemon_attack(t):
    """The cleaver comes down two-handed rather than the Demon's rake."""
    i, k = phases(t, 0.44, 0.62, 1.0)
    raise_ = {"shoulder_l": (-160, 10, 0), "shoulder_r": (-170, -10, 0), "elbow_l": X(-50), "elbow_r": X(-40),
              "chest": X(-14), "wing_l": (0, 0, -46), "wing_r": (0, 0, 46), "_y": 0.08}
    chop = {"shoulder_l": fwd(64), "shoulder_r": fwd(76), "elbow_l": X(-8), "elbow_r": X(-4), "chest": X(30),
            "wing_l": (0, 0, 24), "wing_r": (0, 0, -24), "_y": -0.26, "hip_l": fwd(30), "knee_r": X(22)}
    return [mix({}, raise_, k), mix(raise_, chop, k), mix(chop, {}, k), {}][i]


# =================================================================================
#  Cerberus
# =================================================================================

def _cerb_head(r, j):
    """A head that reads at forty pixels on a black dog: a pale muzzle, the
    mouth open on the fire in its throat, eyes like coals, ears up."""
    r.add("skull", E(0.10, 0.11, 0.095), "cerb", j)
    r.add("muzzle", E(0.062, 0.125, 0.056), "cerb_muzzle", j, loc=(0, -0.15, -0.02))
    r.add("nose", E(0.024, 0.02, 0.02), "cerb_dk", j, loc=(0, -0.27, 0.0))
    r.add("jaw", E(0.052, 0.10, 0.03), "cerb_muzzle", j, loc=(0, -0.14, -0.085))
    r.add("maw", E(0.042, 0.075, 0.022), "cerb_glow", j, loc=(0, -0.16, -0.058))
    for sx in (-1, 1):
        r.limb("ear", (sx * 0.06, 0.03, 0.06), (sx * 0.09, 0.05, 0.19), 0.032, "cerb_dk", j, r_tip=0.006)
        r.add("eye", E(0.028, 0.022, 0.024), "cerb_eye_glow", j, loc=(sx * 0.056, -0.09, 0.04))
        for k in range(2):
            r.limb("tooth", (sx * 0.034, -0.16 - k * 0.045, -0.045), (sx * 0.034, -0.16 - k * 0.045, -0.08), 0.011,
                   "tooth", j, r_tip=0.002)


def _cerb_neck(r, j, at, rest):
    """A neck rising from the chest, in its iron collar, with a head on it."""
    r.joint(j, at, "body", rest=rest)
    r.limb("neckp", (0, 0, 0), (0, -0.08, 0.13), 0.072, "cerb", j, r_tip=0.062)
    r.add("collar", bc.mesh_torus(0.075, 0.02), "cerb_collar", j, loc=(0, -0.02, 0.03), rot=(0.6, 0, 0))
    for k in range(3):
        a = k * 2.0 * math.pi / 3.0 + 0.5
        r.limb("stud", (math.cos(a) * 0.08, -0.02, 0.03 + math.sin(a) * 0.06),
               (math.cos(a) * 0.12, -0.02, 0.03 + math.sin(a) * 0.09), 0.013, "cerb_collar_lt", j, r_tip=0.002)
    head = "head" + j[4:]
    r.joint(head, (0, -0.08, 0.13), j, rest=(-6, 0, 0))
    _cerb_head(r, head)


def build_cerberus():
    """The hound at the gate: a dog the size of a bull, soot-black, three heads
    on three necks each in an iron collar, fire where the throats are and a
    mane of it down the spine -- and for a tail a snake, which is watching you."""
    r = Rig()
    r.joint("body", (0, 0, 0.54), rest=(-4, 0, 0))
    r.add("chest", E(0.18, 0.21, 0.19), "cerb", "body", loc=(0, -0.13, 0.04))
    r.add("belly", E(0.13, 0.22, 0.12), "cerb_belly", "body", loc=(0, 0.06, -0.05))
    r.add("hips", E(0.14, 0.14, 0.14), "cerb", "body", loc=(0, 0.24, 0.03))
    for k in range(5):
        y = -0.14 + k * 0.09
        r.limb("mane", (0, y, 0.16), (0, y + 0.06, 0.28 - k * 0.018), 0.036, "cerb_glow", "body", r_tip=0.006)
    for sx, side in ((-1, "l"), (1, "r")):
        for tag, y in (("fore", -0.17), ("hind", 0.23)):
            j = tag + "_" + side
            r.joint(j, (sx * 0.12, y, -0.06), "body", rest=(8 if tag == "hind" else 4, 0, 0))
            r.limb("thigh", (0, 0, 0), (0, 0.02 if tag == "hind" else -0.02, -0.23), 0.056, "cerb", j, r_tip=0.040)
            r.joint(j + "_knee", (0, 0.02 if tag == "hind" else -0.02, -0.23), j, rest=(-18, 0, 0))
            r.limb("shin", (0, 0, 0), (0, 0, -0.23), 0.036, "cerb_dk", j + "_knee", r_tip=0.026)
            r.add("paw", E(0.05, 0.07, 0.035), "cerb_dk", j + "_knee", loc=(0, -0.02, -0.24))
            for k in (-1, 0, 1):
                r.limb("claw", (k * 0.02, -0.06, -0.25), (k * 0.025, -0.10, -0.27), 0.01, "cerb_collar_lt",
                       j + "_knee", r_tip=0.002)
    # Three necks off the one chest: the middle one forward, the two others
    # set out on its shoulders and turned away from it, so the three heads
    # are three heads from any side and not a knot of one.
    _cerb_neck(r, "neck", (0, -0.28, 0.12), (0, 0, 0))
    _cerb_neck(r, "neck_l", (-0.14, -0.22, 0.10), (0, -16, -34))
    _cerb_neck(r, "neck_r", (0.14, -0.22, 0.10), (0, 16, 34))
    # The tail is a snake.
    r.joint("tail1", (0, 0.35, 0.06), "body", rest=(46, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.22), 0.036, "cerb_snake", "tail1", r_tip=0.03)
    r.joint("tail2", (0, 0, -0.22), "tail1", rest=(-44, 0, 0))
    r.limb("tail", (0, 0, 0), (0, 0, -0.20), 0.03, "cerb_snake_dk", "tail2", r_tip=0.026)
    r.add("snake_head", E(0.04, 0.056, 0.032), "cerb_snake", "tail2", loc=(0, -0.02, -0.23))
    for sx in (-1, 1):
        r.add("snake_eye", E(0.01, 0.01, 0.01), "cerb_eye_glow", "tail2", loc=(sx * 0.024, -0.055, -0.22))
    return r.fit("cerberus", 66)


def _three(fn, loops, lag=0.16):
    """A hound's clip for three heads: the side heads do what the middle one
    does a moment later -- in turn, so a bite is three bites."""
    def pose(t):
        v = dict(fn(t))
        for side, d in (("l", lag), ("r", 2 * lag)):
            w = fn((t + d) % 1.0) if loops else fn(max(0.0, t - d))
            for j in ("neck", "head"):
                if j in w:
                    v[j + "_" + side] = w[j]
        return v
    return pose


cerb_idle = _three(cr.hound_idle, True)
cerb_walk = _three(cr.hound_walk, True)
cerb_attack = _three(cr.hound_attack, False)
cerb_hurt = _three(cr.hound_hurt, False, lag=0.05)
cerb_death = _three(cr.hound_death, False, lag=0.08)


# =================================================================================
#  Registration
# =================================================================================

DRAKE = (cr.drake_idle, cr.drake_walk, cr.drake_attack, cr.drake_hurt, cr.drake_death)
CREATURES = {
    "dragon_fire":      (build_dragon_fire, 144, DRAKE, 0.80),
    "dragon_water":     (build_dragon_water, 144, DRAKE, 0.78),
    "dragon_earth":     (build_dragon_earth, 144, DRAKE, 0.86),
    "dragon_air":       (build_dragon_air, 144, DRAKE, 0.74),
    "dragon_lightning": (build_dragon_lightning, 144, DRAKE, 0.80),
    "greater_demon":    (build_greater_demon, 144,
                         (cr.demon_idle, cr.demon_walk, gdemon_attack, cr.demon_hurt, cr.demon_death), 0.52),
    "cerberus":         (build_cerberus, 128, (cerb_idle, cerb_walk, cerb_attack, cerb_hurt, cerb_death), 0.62),
}
RUNNERS = {"cerberus": 1.22}


def register():
    cr.CREATURES.update(CREATURES)
    cr.RUNNERS.update(RUNNERS)
