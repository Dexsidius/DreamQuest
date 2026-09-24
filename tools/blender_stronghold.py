# =============================================================================
#  blender_stronghold.py - Purgatory's Plateau: the Stronghold on it (walls,
#  towers, gatehouse and keep), the arch at the head of the climb from the
#  Ashen Path, what stands about the plateau, Cerberus's totem -- and the
#  mirror at the bottom of the Dreaming Dark that shows a Havenbrook that is
#  not there.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only stronghold_keep,dream_mirror
#
#  Built with blender_props.py's own tools and registered into its PROPS
#  table, the way blender_palace.py is: that file hands itself over.
#
#  One Blender unit is about one 32px cell. The plateau is not the Ashen Path
#  over again: its stone is pale -- ash-grey basalt bleached by the wind -- its
#  trim is bone, and its fires burn the pale green-white of the dead rather
#  than red.
# =============================================================================

import math

import bpy  # noqa: F401

import blender_props as bp

blk, cyl, cone, sphere = bp.blk, bp.cyl, bp.cone, bp.sphere
BUILDING = bp.BUILDING_ELEVATION

bp.PALETTE.update({
    "sh_stone":     (0.420, 0.400, 0.400),
    "sh_stone_dk":  (0.250, 0.240, 0.250),
    "sh_stone_lt":  (0.560, 0.540, 0.530),
    "sh_mortar":    (0.180, 0.170, 0.180),
    "sh_bone":      (0.880, 0.850, 0.760),
    "sh_bone_dk":   (0.640, 0.600, 0.520),
    "sh_iron":      (0.200, 0.210, 0.230),
    "sh_iron_lt":   (0.380, 0.390, 0.420),
    "sh_soul":      (0.580, 0.980, 0.860),
    "sh_soul_core": (0.920, 1.000, 0.970),
    "sh_banner":    (0.150, 0.210, 0.240),
    "sh_banner_dk": (0.090, 0.130, 0.160),
    "sh_void":      (0.040, 0.050, 0.060),
    "sh_wood":      (0.300, 0.220, 0.170),
    "salt":         (0.930, 0.940, 0.950),
    "salt_dk":      (0.740, 0.780, 0.820),
    "salt_blue":    (0.620, 0.800, 0.900),
    "vent_rock":    (0.300, 0.330, 0.340),
    "vent_rock_dk": (0.180, 0.200, 0.210),
    "steam":        (0.860, 0.960, 0.960),
    "mirror_frame":    (0.600, 0.560, 0.740),
    "mirror_frame_dk": (0.360, 0.320, 0.480),
    "mirror_glass":    (0.560, 0.420, 0.940),
    "mirror_swirl":    (0.900, 0.840, 1.000),
    "mirror_moon":     (0.980, 0.940, 0.760),
    "tt_cerb":   (0.140, 0.110, 0.100),
    "tt_cerb_b": (1.000, 0.460, 0.120),
})


# --- pieces -------------------------------------------------------------------
def soul_flame(name, x, y, z, r, h, emit=1.5):
    """A flame of the plateau's own: pale green-white, with a whiter core."""
    cone(name, r, h, (x, y, z + h / 2), "sh_soul", verts=10, emit=emit)
    cone(name + "_core", r * 0.55, h * 0.7, (x, y - r * 0.2, z + h * 0.35), "sh_soul_core", verts=8, emit=1.8)


def bone_spike(name, x, y, z, r, h, tilt=(0.0, 0.0), colour="sh_bone"):
    cone(name, r, h, (x, y, z + h / 2), colour, verts=6, rot=(tilt[0], tilt[1], 0))


def courses(name, w, d, h, x, y, z0, colour="sh_stone", dark="sh_stone_dk", n=5):
    """A run of masonry: the body, and dark lines between its courses laid
    proud of the face so they show."""
    blk(name, (w, d, h), (x, y, z0 + h / 2), colour, bev=0.03)
    for k in range(1, n):
        z = z0 + h * k / n
        blk("%s_c%d" % (name, k), (w + 0.02, d + 0.02, 0.05), (x, y, z), dark, bev=0)


def merlons(name, w, d, x, y, z, n, colour="sh_stone_lt"):
    """Crenellations along the top of a wall running along x."""
    step = w / n
    for k in range(n):
        if k % 2:
            continue
        blk("%s_%d" % (name, k), (step * 0.9, d, 0.42), (x - w / 2 + step * (k + 0.5), y, z + 0.21), colour, bev=0.02)


def soul_banner(name, x, y, z, w=0.7, h=1.8):
    """A slate-dark banner with a pale eye sewn on it, hung from an iron rod."""
    blk(name + "_rod", (w + 0.3, 0.08, 0.08), (x, y, z + h / 2 + 0.06), "sh_iron", bev=0)
    blk(name, (w, 0.05, h), (x, y, z), "sh_banner", bev=0)
    blk(name + "_hem", (w + 0.02, 0.06, 0.12), (x, y - 0.01, z - h / 2 + 0.08), "sh_bone_dk", bev=0)
    sphere(name + "_eye", 0.13, (x, y - 0.04, z + 0.2), "sh_soul", emit=1.2)
    blk(name + "_lid", (0.36, 0.05, 0.05), (x, y - 0.05, z + 0.36), "sh_bone", bev=0)


# =================================================================================
#  The Stronghold
# =================================================================================

def prop_stronghold_wall():
    """Four cells of curtain wall, running across the screen: pale basalt in
    courses, a dark plinth, merlons along the top and a bone spike between
    each pair."""
    W, D, H = 4.0, 1.0, 3.0
    blk("plinth", (W, D + 0.3, 0.4), (0, 0, 0.2), "sh_stone_dk")
    courses("wall", W, D, H, 0, 0, 0.4)
    blk("walk", (W, D + 0.12, 0.14), (0, 0, 0.4 + H + 0.07), "sh_stone_lt", bev=0.02)
    merlons("merlon", W, D * 0.4, 0, -D * 0.3, 0.4 + H + 0.14, 8)
    for k in range(4):
        bone_spike("spike_%d" % k, -W / 2 + 0.5 + k * 1.0, -D * 0.3, 0.4 + H + 0.14, 0.09, 0.62)
    return (W + 0.4, BUILDING)


def prop_stronghold_wall_v():
    """The same wall, running up the screen: seen along its length, so what
    shows is its face on one side, the walk along its top, and the merlons."""
    W, D, H = 1.0, 4.0, 3.0
    blk("plinth", (W + 0.3, D, 0.4), (0, 0, 0.2), "sh_stone_dk")
    courses("wall", W, D, H, 0, 0, 0.4)
    blk("walk", (W + 0.12, D, 0.14), (0, 0, 0.4 + H + 0.07), "sh_stone_lt", bev=0.02)
    for k in range(8):
        if k % 2:
            continue
        blk("merlon_%d" % k, (W * 0.4, D / 8 * 0.9, 0.42), (-W * 0.3, -D / 2 + D / 8 * (k + 0.5), 0.4 + H + 0.35),
            "sh_stone_lt", bev=0.02)
    for k in range(4):
        bone_spike("spike_%d" % k, -W * 0.3, -D / 2 + 0.5 + k * 1.0, 0.4 + H + 0.14, 0.09, 0.62)
    return (3.0, BUILDING)


def prop_stronghold_tower():
    """A corner tower: square, battered at the foot, crenellated, with a brazier
    of pale fire on top and ribs of bone curving up from its corners."""
    S, H = 2.4, 5.2
    blk("batter", (S + 0.6, S + 0.6, 0.9), (0, 0, 0.45), "sh_stone_dk")
    courses("body", S, S, H, 0, 0, 0.9, n=7)
    blk("cap", (S + 0.3, S + 0.3, 0.2), (0, 0, 0.9 + H + 0.1), "sh_stone_lt", bev=0.02)
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("merlon_%d_%d" % (sx, sy), (0.62, 0.62, 0.5), (sx * (S / 2 - 0.1), sy * (S / 2 - 0.1), 0.9 + H + 0.45),
                "sh_stone_lt", bev=0.02)
            cone("rib_%d_%d" % (sx, sy), 0.12, 1.8, (sx * (S / 2 + 0.15), sy * (S / 2 + 0.15), 0.9 + H + 0.5),
                 "sh_bone", verts=6, rot=(sy * -0.35, sx * 0.35, 0))
    blk("slit", (0.16, 0.06, 0.8), (0, -S / 2 - 0.02, 0.9 + H * 0.62), "sh_soul", emit=1.2, bev=0)
    blk("slit2", (0.16, 0.06, 0.6), (0, -S / 2 - 0.02, 0.9 + H * 0.3), "sh_void", bev=0)
    cyl("brazier", 0.55, 0.3, (0, 0, 0.9 + H + 0.35), "sh_iron", verts=10)
    soul_flame("fire", 0, 0, 0.9 + H + 0.5, 0.45, 1.2)
    soul_banner("banner", 0, -S / 2 - 0.06, 0.9 + H * 0.5 - 0.3, w=0.6, h=1.5)
    return (S + 3.0, BUILDING)


def prop_stronghold_gate():
    """The gatehouse: two squat towers either side of an arched way in, the
    portcullis raised into the dark over it, braziers of pale fire on the
    towers and a great skull of something with horns over the arch."""
    GW = 2.6                                   # the way through
    TW, TD, TH = 2.2, 2.4, 4.6
    for s in (-1, 1):
        x = s * (GW / 2 + TW / 2)
        blk("batter_%d" % s, (TW + 0.4, TD + 0.4, 0.8), (x, 0, 0.4), "sh_stone_dk")
        courses("tower_%d" % s, TW, TD, TH, x, 0, 0.8, n=6)
        blk("tcap_%d" % s, (TW + 0.24, TD + 0.24, 0.2), (x, 0, 0.8 + TH + 0.1), "sh_stone_lt", bev=0.02)
        for k in (-1, 1):
            blk("tmerlon_%d_%d" % (s, k), (0.6, TD * 0.5, 0.46), (x + k * 0.72, -TD * 0.2, 0.8 + TH + 0.43),
                "sh_stone_lt", bev=0.02)
        cyl("brazier_%d" % s, 0.42, 0.28, (x, 0.2, 0.8 + TH + 0.34), "sh_iron", verts=10)
        soul_flame("fire_%d" % s, x, 0.2, 0.8 + TH + 0.48, 0.34, 1.0)
        soul_banner("banner_%d" % s, x, -TD / 2 - 0.05, 0.8 + TH * 0.52, w=0.6, h=1.7)
    # The arch over the way: its lintel, the raised portcullis in the dark
    # under it, and the way itself open.
    AH = 3.3
    blk("lintel", (GW + 0.4, TD, 1.4), (0, 0, 0.8 + AH + 0.7), "sh_stone", bev=0.03)
    blk("lintel_c", (GW + 0.42, TD + 0.02, 0.06), (0, 0, 0.8 + AH + 0.2), "sh_stone_dk", bev=0)
    blk("arch_void", (GW, 0.1, 0.9), (0, -TD / 2 + 0.03, 0.8 + AH - 0.35), "sh_void", bev=0)
    for k in range(6):
        x = -GW / 2 + 0.22 + k * (GW - 0.44) / 5
        cone("tooth_%d" % k, 0.07, 0.34, (x, -TD / 2 + 0.1, 0.8 + AH - 0.72), "sh_iron", verts=4,
             rot=(math.pi, 0, 0))
    blk("port_bar", (GW - 0.1, 0.08, 0.1), (0, -TD / 2 + 0.1, 0.8 + AH - 0.52), "sh_iron", bev=0)
    # The skull over it.
    sphere("skull", 0.55, (0, -TD / 2 - 0.15, 0.8 + AH + 1.05), "sh_bone")
    blk("snout", (0.5, 0.5, 0.36), (0, -TD / 2 - 0.5, 0.8 + AH + 0.8), "sh_bone_dk")
    for s in (-1, 1):
        sphere("socket_%d" % s, 0.13, (s * 0.2, -TD / 2 - 0.62, 0.8 + AH + 1.12), "sh_soul", emit=1.4)
        cone("horn_%d" % s, 0.16, 1.3, (s * 0.75, -TD / 2 - 0.2, 0.8 + AH + 1.5), "sh_bone", verts=8,
             rot=(0, s * 0.9, 0))
    return (GW + 2 * TW + 1.6, BUILDING)


def prop_stronghold_keep():
    """The keep: a tall block of pale basalt with a taller tower behind, bone
    along every edge, windows lit green-white, and at the front a doorway that
    stands out of the wall on its own steps -- dark, with the doors thrown open
    on pale light, and the eye of the banners carved over it."""
    W, D, H = 9.0, 4.0, 5.4
    front = -D / 2
    base = 0.6
    blk("plinth", (W + 0.6, D + 0.6, base), (0, 0, base / 2), "sh_stone_dk")
    courses("body", W, D, H, 0, 0, base, n=7)
    blk("cornice", (W + 0.3, D + 0.3, 0.2), (0, 0, base + H + 0.1), "sh_stone_lt", bev=0.02)
    merlons("merlon", W, 0.5, 0, front + 0.25, base + H + 0.2, 14)
    for k in range(6):
        bone_spike("spike_%d" % k, -W / 2 + 0.7 + k * (W - 1.4) / 5, front + 0.25, base + H + 0.2, 0.1, 0.8)
    # Buttresses, and windows between them.
    for s in (-1, 1):
        for k, x in enumerate((2.3, 4.2)):
            blk("butt_%d_%d" % (s, k), (0.5, 0.6, H), (s * x, front - 0.22, base + H / 2), "sh_stone_dk")
            bone_spike("butt_spike_%d_%d" % (s, k), s * x, front - 0.22, base + H, 0.2, 1.0)
        blk("win_%d" % s, (0.42, 0.06, 1.1), (s * 3.25, front - 0.02, base + 3.4), "sh_soul", emit=1.2, bev=0)
        blk("win_frame_%d" % s, (0.6, 0.05, 1.3), (s * 3.25, front - 0.01, base + 3.4), "sh_bone_dk", bev=0)
        soul_banner("banner_%d" % s, s * 1.5, front - 0.05, base + 3.2, w=0.7, h=2.0)
    # The tower behind.
    TS, TH = 3.2, 9.6
    courses("tower", TS, TS, TH, 0, 0.5, base, n=10)
    blk("tcap", (TS + 0.3, TS + 0.3, 0.2), (0, 0.5, base + TH + 0.1), "sh_stone_lt", bev=0.02)
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("tmerlon_%d_%d" % (sx, sy), (0.7, 0.7, 0.5), (sx * (TS / 2 - 0.1), 0.5 + sy * (TS / 2 - 0.1),
                                                                base + TH + 0.45), "sh_stone_lt", bev=0.02)
            cone("trib_%d_%d" % (sx, sy), 0.14, 2.2, (sx * (TS / 2 + 0.15), 0.5 + sy * (TS / 2 + 0.15),
                                                      base + TH + 0.6), "sh_bone", verts=6,
                 rot=(sy * -0.35, sx * 0.35, 0))
    blk("twin", (0.5, 0.06, 1.3), (0, 0.5 - TS / 2 - 0.02, base + TH - 1.6), "sh_soul", emit=1.3, bev=0)
    cyl("tbrazier", 0.7, 0.3, (0, 0.5, base + TH + 0.35), "sh_iron", verts=12)
    soul_flame("tfire", 0, 0.5, base + TH + 0.5, 0.6, 1.6)
    # The doorway, standing proud of the front: its own block on its own
    # steps, the opening cut through it as a dark void faced forward, the
    # doors thrown back either side on pale light.
    gw, gd, gh = 3.0, 1.2, 3.6
    gy = front - gd / 2
    blk("door_block", (gw, gd, gh), (0, gy, base + gh / 2), "sh_stone_lt", bev=0.03)
    for k in range(3):
        blk("step_%d" % k, (gw + 0.6 - k * 0.2, 0.4, 0.2), (0, gy - gd / 2 - 0.2 - (2 - k) * 0.36, 0.1 + k * 0.2),
            "sh_stone_dk")
    blk("doorway", (1.4, 0.1, 2.2), (0, gy - gd / 2 - 0.02, base + 1.1), "sh_void", bev=0)
    blk("door_light", (0.9, 0.1, 1.8), (0, gy - gd / 2 - 0.01, base + 0.95), "sh_soul", emit=0.8, bev=0)
    for s in (-1, 1):
        blk("leaf_%d" % s, (0.1, 0.62, 2.0), (s * 0.72, gy - gd / 2 - 0.3, base + 1.0), "sh_wood", bev=0.01)
        blk("jamb_%d" % s, (0.22, 0.14, 2.4), (s * 0.82, gy - gd / 2 - 0.05, base + 1.2), "sh_bone_dk", bev=0.01)
    blk("lintel", (2.0, 0.16, 0.26), (0, gy - gd / 2 - 0.05, base + 2.35), "sh_bone_dk", bev=0.01)
    sphere("door_eye", 0.26, (0, gy - gd / 2 - 0.08, base + 2.95), "sh_soul", emit=1.3)
    blk("door_lid", (0.7, 0.08, 0.1), (0, gy - gd / 2 - 0.1, base + 3.24), "sh_bone", bev=0)
    return (13.0, BUILDING)


def prop_soul_brazier():
    """An iron brazier on three legs, full of the pale fire."""
    for k in range(3):
        a = k / 3 * math.tau
        blk("leg_%d" % k, (0.08, 0.08, 0.9), (math.cos(a) * 0.28, math.sin(a) * 0.28, 0.45), "sh_iron",
            rot=(math.sin(a) * 0.25, -math.cos(a) * 0.25, 0), bev=0)
    cone("bowl", 0.44, 0.34, (0, 0, 0.98), "sh_iron", verts=12, rot=(math.pi, 0, 0))
    cyl("rim", 0.46, 0.07, (0, 0, 1.14), "sh_iron_lt", verts=12)
    soul_flame("fire", 0, 0, 1.12, 0.34, 0.9)
    return 2.2


# =================================================================================
#  The plateau
# =================================================================================

def prop_purgatory_arch():
    """The way up onto the plateau: two pillars of pale basalt, each hung with
    a lantern of pale fire, and between them the ribs of something enormous
    set up as an arch -- one bone rising from each pillar and curving in, meeting
    over the path under a skull. The way through is open."""
    for s in (-1, 1):
        x = s * 2.2
        blk("pier_base_%d" % s, (1.3, 1.3, 0.5), (x, 0, 0.25), "sh_stone_dk")
        courses("pier_%d" % s, 1.0, 1.0, 4.2, x, 0, 0.5, n=6)
        blk("pier_cap_%d" % s, (1.2, 1.2, 0.24), (x, 0, 4.82), "sh_stone_lt", bev=0.02)
        blk("hook_%d" % s, (0.5, 0.08, 0.08), (x - s * 0.55, -0.3, 4.1), "sh_iron", bev=0)
        blk("chain_%d" % s, (0.04, 0.04, 0.4), (x - s * 0.8, -0.3, 3.9), "sh_iron", bev=0)
        blk("lantern_%d" % s, (0.26, 0.26, 0.34), (x - s * 0.8, -0.3, 3.55), "sh_soul", emit=1.5, bev=0)
        # The rib: a curve from the pillar's cap in to the middle, laid as
        # overlapping lengths with a knuckle at every joint so it reads as one
        # bone and not as a row of sticks.
        pts = []
        for k in range(9):
            t = k / 8.0
            pts.append((x * (1.0 - t) ** 1.15, 4.95 + 2.15 * math.sin(t * math.pi / 2.0)))
        for k, ((x0, z0), (x1, z1)) in enumerate(zip(pts, pts[1:])):
            dx, dz = x1 - x0, z1 - z0
            length = math.hypot(dx, dz) * 1.25
            r = 0.26 - 0.012 * k
            cyl("rib_%d_%d" % (s, k), r, length, ((x0 + x1) / 2, 0, (z0 + z1) / 2), "sh_bone", verts=10,
                rot=(0, math.atan2(dx, dz), 0))
            sphere("knuckle_%d_%d" % (s, k), r * 1.08, (x1, 0, z1), "sh_bone" if k % 2 else "sh_bone_dk")
    sphere("skull", 0.52, (0, -0.05, 7.35), "sh_bone")
    blk("skull_jaw", (0.52, 0.42, 0.24), (0, -0.28, 6.98), "sh_bone_dk")
    for s in (-1, 1):
        sphere("eye_%d" % s, 0.11, (s * 0.18, -0.44, 7.4), "sh_soul", emit=1.5)
    return (8.6, BUILDING)


def prop_bone_spire():
    """Ribs and spines of long-dead things standing out of the ash, bleached."""
    for k, (x, y, r, h, t) in enumerate(((0, 0, 0.26, 2.8, (0.05, 0.1)), (0.35, 0.2, 0.18, 1.9, (0.1, 0.4)),
                                         (-0.32, 0.12, 0.16, 1.6, (-0.1, -0.45)), (0.1, -0.3, 0.12, 1.1, (-0.4, 0.2)))):
        bone_spike("spire_%d" % k, x, y, 0.0, r, h, tilt=t, colour="sh_bone" if k % 2 == 0 else "sh_bone_dk")
    # Fragments of the rest of whatever it was, lying round its foot.
    for k, (x, y, rot) in enumerate(((0.45, -0.25, 0.4), (-0.4, -0.3, -0.7), (0.1, 0.4, 1.2))):
        blk("fragment_%d" % k, (0.34, 0.08, 0.08), (x, y, 0.04), "sh_bone_dk", rot=(0, 0, rot), bev=0.02)
    return 3.2


def prop_salt_pillar():
    """Columns of salt left standing where the flats blew away round them:
    three six-sided shafts of different heights, white, one gone pale blue,
    their tops broken off at a slant."""
    blk("base", (1.3, 1.1, 0.3), (0, 0, 0.15), "salt_dk", bev=0.06)
    for name, x, y, r, h, col in (("tall", 0.0, 0.05, 0.40, 2.7, "salt"), ("mid", 0.46, 0.22, 0.30, 1.8, "salt_dk"),
                                   ("low", -0.42, -0.08, 0.28, 1.2, "salt_blue")):
        cyl(name, r, h, (x, y, 0.3 + h / 2), col, verts=6)
        blk(name + "_cap", (r * 1.7, r * 1.7, 0.18), (x, y, 0.3 + h), col, rot=(0.35, 0.2, 0.4), bev=0.02)
    blk("chunk", (0.34, 0.3, 0.24), (0.55, -0.4, 0.12), "salt", rot=(0, 0, 0.6), bev=0.04)
    return 3.0


def prop_steam_vent():
    """A cone of dark rock with a crack down it, and the pale steam of the
    terraces' brine coming out of the top in a column."""
    cone("mound", 0.8, 0.8, (0, 0, 0.4), "vent_rock", verts=10)
    blk("crack", (0.1, 0.08, 0.6), (0.1, -0.52, 0.34), "sh_soul", emit=1.1, rot=(0.3, 0, 0.2), bev=0)
    cyl("mouth", 0.26, 0.1, (0, 0, 0.8), "vent_rock_dk", verts=10)
    for k, (r, z) in enumerate(((0.24, 1.1), (0.32, 1.55), (0.28, 2.0), (0.18, 2.35))):
        sphere("steam_%d" % k, r, (0.05 * k, 0.03 * k, z), "steam", emit=0.9)
    return 2.8


def prop_dragon_skull():
    """The skull of a dragon bigger than any alive on the plateau, lying where
    it fell with its snout toward you: a long toothed muzzle, eye sockets you
    could stand in, horns swept back along the ground, and a few bones of the
    neck behind it. The lower jaw is gone."""
    blk("cranium", (1.9, 1.7, 1.0), (0, 0.9, 0.5), "sh_bone", bev=0.28)
    blk("muzzle", (1.2, 1.4, 0.62), (0, -0.55, 0.34), "sh_bone", bev=0.18)
    blk("snout", (0.86, 1.1, 0.46), (0, -1.65, 0.26), "sh_bone", bev=0.14)
    blk("tip", (0.6, 0.4, 0.36), (0, -2.3, 0.22), "sh_bone_dk", bev=0.1)
    for s in (-1, 1):
        blk("brow_%d" % s, (0.5, 0.9, 0.26), (s * 0.58, 0.3, 1.02), "sh_bone_dk", rot=(0, 0, s * 0.25), bev=0.08)
        blk("socket_%d" % s, (0.46, 0.56, 0.24), (s * 0.58, 0.25, 0.88), "sh_void", bev=0.05)
        blk("nostril_%d" % s, (0.14, 0.2, 0.1), (s * 0.18, -2.1, 0.42), "sh_void", bev=0)
        cone("horn_%d" % s, 0.26, 2.6, (s * 0.85, 2.2, 0.5), "sh_bone_dk", verts=8, rot=(-1.45, s * 0.25, 0))
        cone("horn2_%d" % s, 0.16, 1.4, (s * 0.95, 1.4, 0.95), "sh_bone", verts=8, rot=(-1.2, s * 0.4, 0))
        for k in range(5):
            cone("tooth_%d_%d" % (s, k), 0.075, 0.34, (s * 0.46, -2.15 + k * 0.38, 0.02), "sh_bone", verts=5,
                 rot=(math.pi, 0, 0))
    for k in range(3):
        sphere("neck_%d" % k, 0.34 - k * 0.05, (0.1 * k, 2.1 + k * 0.55, 0.25), "sh_bone_dk")
    return 5.6


# =================================================================================
#  The mirror at the bottom of the dream
# =================================================================================

def prop_dream_mirror():
    """A tall standing mirror on clawed feet, its frame silver gone violet, a
    crescent moon on its crest -- and the glass not showing what is in front
    of it: a slow violet swirl, lit from somewhere behind."""
    for s in (-1, 1):
        blk("foot_%d" % s, (0.5, 0.7, 0.16), (s * 0.55, 0, 0.08), "mirror_frame_dk")
        for k in (-1, 0, 1):
            cone("claw_%d_%d" % (s, k), 0.06, 0.24, (s * 0.55 + k * 0.14, -0.36, 0.06), "mirror_frame_dk", verts=5,
                 rot=(math.pi / 2, 0, 0))
        blk("post_%d" % s, (0.16, 0.16, 3.2), (s * 0.78, 0.05, 1.7), "mirror_frame", bev=0.03)
        sphere("knob_%d" % s, 0.14, (s * 0.78, 0.05, 3.36), "mirror_frame_dk")
    # The oval: an ellipse of frame, the glass inside it, and the swirl on it.
    ob = cyl("frame", 0.7, 0.14, (0, 0, 1.9), "mirror_frame", verts=28, rot=(math.pi / 2, 0, 0))
    ob.scale = (1.0, 1.55, 1.0)
    ob = cyl("glass", 0.58, 0.16, (0, -0.01, 1.9), "mirror_glass", verts=28, rot=(math.pi / 2, 0, 0), emit=1.2)
    ob.scale = (1.0, 1.55, 1.0)
    for k in range(5):
        a = k * 1.3
        sphere("swirl_%d" % k, 0.08 + 0.02 * k, (math.cos(a) * 0.12 * k, -0.1, 1.9 + math.sin(a) * 0.16 * k),
               "mirror_swirl", emit=1.4)
    # The crest: a crescent, and a point above it.
    ob = cyl("moon", 0.3, 0.1, (0, -0.02, 3.2), "mirror_moon", verts=20, rot=(math.pi / 2, 0, 0), emit=0.8)
    cyl("moon_bite", 0.26, 0.12, (0.12, -0.03, 3.28), "mirror_frame_dk", verts=20, rot=(math.pi / 2, 0, 0))
    cone("point", 0.08, 0.4, (0, 0, 3.58), "mirror_frame", verts=6)
    return 3.9


# =================================================================================
#  Cerberus's totem
# =================================================================================

def prop_totem_cerberus():
    """Cerberus's totem: a soot-black post banded in fire, hung with three iron
    collars, and on top three small dog heads, open-mouthed."""
    r = bp._totem("tt_cerb", "tt_cerb_b", "ears", eye_emit=1.7)
    for k, z in enumerate((0.34, 0.50, 0.66)):
        cyl("collar_%d" % k, 0.24, 0.05, (0, 0, z), "sh_iron_lt", verts=12, metal=0.4)
    for k, (x, turn) in enumerate(((-0.16, -0.5), (0.0, 0.0), (0.16, 0.5))):
        sphere("dog_%d" % k, 0.09, (x, -0.05 + abs(x) * 0.3, 1.14), "tt_cerb")
        blk("muzzle_%d" % k, (0.07, 0.12, 0.06), (x + turn * 0.06, -0.14, 1.12), "tt_cerb", rot=(0, 0, turn), bev=0)
        blk("maw_%d" % k, (0.05, 0.04, 0.02), (x + turn * 0.07, -0.2, 1.09), "tt_cerb_b", emit=1.4, bev=0)
    return r


# =================================================================================
#  Registration: name -> (builder, final pixels across)
# =================================================================================
PROPS = {
    "stronghold_wall":   (prop_stronghold_wall, 144),
    "stronghold_wall_v": (prop_stronghold_wall_v, 96),
    "stronghold_tower":  (prop_stronghold_tower, 172),
    "stronghold_gate":   (prop_stronghold_gate, 256),
    "stronghold_keep":   (prop_stronghold_keep, 416),
    "soul_brazier":      (prop_soul_brazier, 70),
    "purgatory_arch":    (prop_purgatory_arch, 276),
    "bone_spire":        (prop_bone_spire, 102),
    "salt_pillar":       (prop_salt_pillar, 96),
    "steam_vent":        (prop_steam_vent, 90),
    "dragon_skull":      (prop_dragon_skull, 180),
    "dream_mirror":      (prop_dream_mirror, 124),
    "totem_cerberus":    (prop_totem_cerberus, 32),
}
