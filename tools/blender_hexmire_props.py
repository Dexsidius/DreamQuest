# =============================================================================
#  blender_hexmire_props.py - the Hexmire, north of the Bayou: the gateway the
#  cult hung across the old track, the cypress drowns, the Shellbacks' strand
#  (their shell-roofed huts, the middens and the driftwood), the cult's fens
#  (fetish poles, candle shrines, drums, a tree hung with bottles), the
#  stockade round the temple, the temple itself, what stands in its sanctum,
#  and the High Priest's totem.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only hex_temple,cypress_tree
#
#  Built with blender_props.py's own tools and registered into its PROPS
#  table, the way blender_stronghold.py is: that file hands itself over.
#
#  One Blender unit is about one 32px cell. The cult's colours are few and
#  loud against the swamp's drab: black cypress, bone, a red like old blood,
#  chalk white, and a sickly green in its jars and candles.
# =============================================================================

import math

import bpy
from mathutils import Vector

import blender_props as bp

blk, cyl, cone, sphere = bp.blk, bp.cyl, bp.cone, bp.sphere
BUILDING = bp.BUILDING_ELEVATION

bp.PALETTE.update({
    "hx_post":      (0.150, 0.120, 0.100),
    "hx_post_lt":   (0.260, 0.210, 0.170),
    "hx_bark":      (0.380, 0.260, 0.190),
    "hx_bark_dk":   (0.240, 0.160, 0.120),
    "hx_bark_lt":   (0.520, 0.380, 0.280),
    "hx_leaf":      (0.340, 0.420, 0.220),
    "hx_leaf_dk":   (0.220, 0.290, 0.150),
    "hx_leaf_rust": (0.560, 0.380, 0.180),
    "hx_moss":      (0.580, 0.620, 0.520),
    "hx_moss_dk":   (0.420, 0.460, 0.380),
    "hx_bone":      (0.880, 0.840, 0.740),
    "hx_bone_dk":   (0.640, 0.590, 0.500),
    "hx_red":       (0.600, 0.120, 0.100),
    "hx_red_dk":    (0.360, 0.070, 0.060),
    "hx_chalk":     (0.920, 0.900, 0.840),
    "hx_rope":      (0.620, 0.520, 0.340),
    "hx_cloth":     (0.520, 0.440, 0.320),
    "hx_cloth_dk":  (0.360, 0.300, 0.220),
    "hx_feather":   (0.180, 0.160, 0.170),
    "hx_feather_w": (0.900, 0.880, 0.820),
    "hx_glow":      (0.620, 1.000, 0.420),
    "hx_glow_core": (0.900, 1.000, 0.760),
    "hx_flame":     (1.000, 0.700, 0.260),
    "hx_flame_core": (1.000, 0.940, 0.640),
    "hx_wax":       (0.900, 0.860, 0.720),
    "hx_bottle_b":  (0.220, 0.420, 0.900),
    "hx_bottle_g":  (0.260, 0.720, 0.420),
    "hx_bottle_a":  (0.820, 0.520, 0.180),
    "hx_bottle_v":  (0.560, 0.300, 0.820),
    "hx_hide":      (0.760, 0.680, 0.540),
    "hx_thatch":    (0.560, 0.450, 0.260),
    "hx_thatch_dk": (0.380, 0.300, 0.170),
    "hx_daub":      (0.600, 0.440, 0.300),
    "hx_daub_dk":   (0.420, 0.300, 0.200),
    "hx_void":      (0.050, 0.040, 0.040),
    "hx_mud":       (0.320, 0.260, 0.180),
    "hx_shell":     (0.560, 0.420, 0.220),
    "hx_shell_dk":  (0.300, 0.220, 0.120),
    "hx_shell_lt":  (0.760, 0.620, 0.360),
    "hx_conch":     (0.920, 0.780, 0.700),
    "hx_conch_in":  (0.960, 0.600, 0.560),
    "hx_drift":     (0.620, 0.580, 0.520),
    "hx_drift_dk":  (0.440, 0.400, 0.350),
    "hx_kelp":      (0.300, 0.380, 0.200),
    "tt_hex":       (0.160, 0.120, 0.100),
    "tt_hex_b":     (0.600, 1.000, 0.420),
})


# --- pieces -------------------------------------------------------------------
def frustum(name, r1, r2, h, loc, colour, rot=(0, 0, 0), verts=12, emit=0.0):
    """A cone cut off: a trunk that tapers, a drum, a pot."""
    bpy.ops.mesh.primitive_cone_add(radius1=r1, radius2=r2, depth=h, location=loc, vertices=verts)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = rot
    ob.data.materials.append(bp.material(name, colour, 0.8, 0.0, emit))
    return ob


def rod(name, p0, p1, r, colour, verts=8, emit=0.0):
    """A round length from one point to another: a branch, a pole, a strand."""
    dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    mid = ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, (p0[2] + p1[2]) / 2)
    ob = cyl(name, r, length, mid, colour, verts=verts, emit=emit)
    # Point the cylinder's Z along the rod: tip it over about X, which lays
    # it toward -Y, then turn it about Z to face the way the rod goes.
    yaw = math.atan2(dx, -dy) if (dx or dy) else 0.0
    ob.rotation_euler = (math.acos(max(-1.0, min(1.0, dz / length))) if length else 0.0, 0.0, yaw)
    return ob


def blob(name, r, loc, colour, scale=(1, 1, 1), emit=0.0):
    ob = sphere(name, r, loc, colour, emit=emit)
    ob.scale = scale
    return ob


def skull(name, x, y, z, s=1.0, eyes="hx_void", emit=0.0):
    """A small skull facing the camera: crown, a narrower jaw, two sockets."""
    blob(name, 0.16 * s, (x, y, z), "hx_bone", (1.0, 0.95, 1.0))
    blk(name + "_jaw", (0.18 * s, 0.14 * s, 0.09 * s), (x, y - 0.03 * s, z - 0.13 * s), "hx_bone_dk", bev=0.01)
    for sx in (-1, 1):
        blk("%s_eye%d" % (name, sx), (0.06 * s, 0.03 * s, 0.06 * s), (x + sx * 0.06 * s, y - 0.15 * s, z + 0.0),
            eyes, emit=emit, bev=0)


def jar(name, x, y, z, colour="hx_glow", s=1.0, emit=1.4):
    """A glass jar with a light in it, hung on a cord."""
    blk(name, (0.16 * s, 0.16 * s, 0.22 * s), (x, y, z), colour, emit=emit, bev=0.02)
    blk(name + "_lid", (0.18 * s, 0.18 * s, 0.05 * s), (x, y, z + 0.13 * s), "hx_post", bev=0)


def feathers(name, x, y, z, n=3, length=0.34, spread=0.5, colour="hx_feather"):
    for k in range(n):
        a = (k - (n - 1) / 2) * spread
        blk("%s_%d" % (name, k), (0.07, 0.03, length), (x + math.sin(a) * length * 0.4, y, z - length * 0.45),
            colour if k % 2 == 0 else "hx_feather_w", rot=(0, a, 0), bev=0)


def bottle(name, x, y, z, colour, s=1.0, rot=(0, 0, 0), emit=0.6):
    blk(name, (0.12 * s, 0.12 * s, 0.24 * s), (x, y, z), colour, rot=rot, emit=emit, bev=0.02)
    blk(name + "_neck", (0.05 * s, 0.05 * s, 0.12 * s), (x, y, z + 0.17 * s), colour, rot=rot, emit=emit, bev=0)


def candle(name, x, y, z, h=0.24, r=0.05):
    cyl(name, r, h, (x, y, z + h / 2), "hx_wax", verts=8)
    cone(name + "_flame", r * 0.9, 0.12, (x, y, z + h + 0.06), "hx_flame", verts=6, emit=1.6)


def stake(name, x, y, h, r=0.12, colour="hx_post", tip="hx_bone_dk", lean=(0.0, 0.0)):
    cyl(name, r, h, (x, y, h / 2), colour, verts=7, rot=(lean[0], lean[1], 0))
    cone(name + "_tip", r * 1.05, 0.34, (x + lean[1] * h * 0.5, y - lean[0] * h * 0.5, h + 0.16), tip, verts=7,
         rot=(lean[0], lean[1], 0))


# =================================================================================
#  The way in, from the Bayou
# =================================================================================

def prop_hex_gateway():
    """The cult's gateway across the old track: two black cypress posts and a
    beam lashed across them, and hung from the beam on cords everything the
    cult hangs up -- skulls, bottles, bundles of feathers, rags -- with a jar of
    green light at each end and a great horned skull nailed over the middle.
    The way under it is open."""
    for s in (-1, 1):
        x = s * 2.3
        cyl("post_%d" % s, 0.28, 5.4, (x, 0, 2.7), "hx_post", verts=9)
        cyl("post_band_%d" % s, 0.30, 0.3, (x, 0, 1.4), "hx_red", verts=9)
        cyl("post_band2_%d" % s, 0.30, 0.16, (x, 0, 3.6), "hx_chalk", verts=9)
        cone("post_tip_%d" % s, 0.3, 0.6, (x, 0, 5.7), "hx_post", verts=9)
        skull("post_skull_%d" % s, x, -0.3, 4.6, 1.3)
        blk("stones_%d" % s, (0.9, 0.9, 0.3), (x, 0, 0.15), "hx_mud")
        # A jar of green light on a bracket, out toward the path.
        blk("bracket_%d" % s, (0.6, 0.08, 0.08), (x - s * 0.4, -0.2, 4.2), "hx_post", bev=0)
        blk("cord_j_%d" % s, (0.03, 0.03, 0.4), (x - s * 0.62, -0.2, 3.98), "hx_rope", bev=0)
        jar("jar_%d" % s, x - s * 0.62, -0.2, 3.66, s=1.5)
    blk("beam", (5.8, 0.34, 0.36), (0, 0, 4.95), "hx_post_lt", bev=0.03)
    blk("beam2", (5.2, 0.3, 0.26), (0, 0.05, 5.3), "hx_post", bev=0.02)
    for s in (-1, 1):
        for k in range(3):
            blk("lash_%d_%d" % (s, k), (0.62, 0.40, 0.05), (s * 2.3, 0, 4.82 + k * 0.12), "hx_rope", bev=0)
    # The horned skull over the middle.
    blob("big_skull", 0.42, (0, -0.25, 5.35), "hx_bone", (1.1, 0.9, 1.0))
    blk("big_snout", (0.36, 0.32, 0.26), (0, -0.52, 5.12), "hx_bone_dk")
    for s in (-1, 1):
        blk("big_eye_%d" % s, (0.12, 0.05, 0.12), (s * 0.15, -0.62, 5.38), "hx_glow", emit=1.5, bev=0)
        cone("horn_%d" % s, 0.12, 1.1, (s * 0.7, -0.2, 5.75), "hx_bone", verts=8, rot=(0, s * 1.0, 0))
    # Hung along the beam: a strand of charms every so often, of different lengths.
    hung = [(-1.6, "skull"), (-1.05, "bottle_b"), (-0.55, "feathers"), (0.55, "bottle_g"), (1.05, "skull"),
            (1.6, "feathers")]
    for k, (x, what) in enumerate(hung):
        drop = 0.5 + 0.25 * (k % 3)
        blk("cord_%d" % k, (0.03, 0.03, drop), (x, -0.16, 4.78 - drop / 2), "hx_rope", bev=0)
        z = 4.78 - drop - 0.12
        if what == "skull":
            skull("hung_skull_%d" % k, x, -0.18, z, 0.9)
        elif what == "feathers":
            feathers("hung_f_%d" % k, x, -0.18, z + 0.1, n=3, length=0.4)
            blk("hung_f_bind_%d" % k, (0.1, 0.1, 0.1), (x, -0.18, z + 0.12), "hx_red", bev=0)
        else:
            bottle("hung_b_%d" % k, x, -0.18, z - 0.02, "hx_" + what, s=1.3)
        blk("rag_%d" % k, (0.14, 0.03, 0.3), (x + 0.12, -0.17, 4.6), "hx_red" if k % 2 else "hx_cloth", bev=0)
    return (7.2, BUILDING)


# =================================================================================
#  The Cypress Drowns
# =================================================================================

def prop_cypress_tree():
    """A bald cypress: a trunk flared into buttresses at the foot, going up
    straight and tapering, a broad, ragged crown in layers, rust showing through
    the green, and grey moss hanging off it in strands."""
    frustum("flare", 0.95, 0.35, 1.1, (0, 0, 0.55), "hx_bark", verts=10)
    for k in range(5):
        a = k / 5 * math.tau + 0.3
        rod("root_%d" % k, (math.cos(a) * 0.3, math.sin(a) * 0.3, 0.85),
            (math.cos(a) * 1.05, math.sin(a) * 1.05, 0.05), 0.17, "hx_bark_dk")
    frustum("trunk", 0.36, 0.2, 3.2, (0, 0, 2.6), "hx_bark", verts=10)
    for k in range(3):
        blk("groove_%d" % k, (0.06, 0.05, 2.6), ((k - 1) * 0.16, -0.3 + abs(k - 1) * 0.04, 2.3), "hx_bark_dk", bev=0)
    for k, (x, y, z) in enumerate(((-1.2, 0.1, 3.7), (1.3, -0.1, 3.9), (-0.5, 0.3, 4.4), (0.8, 0.2, 4.3))):
        rod("branch_%d" % k, (0, 0, z - 0.8), (x, y, z), 0.09, "hx_bark_dk")
    # The crown: rounded clumps in two layers, the lower one wider, each with
    # a patch of the rust a cypress goes in autumn.
    clumps = [(-1.55, 0.1, 3.8, 0.9), (1.55, -0.1, 3.95, 0.95), (-0.7, -0.25, 3.9, 0.85), (0.75, -0.3, 4.0, 0.85),
              (0.0, 0.2, 4.55, 1.15), (-0.9, 0.3, 4.6, 0.8), (0.95, 0.3, 4.65, 0.8), (0.1, -0.1, 5.05, 0.8)]
    for k, (x, y, z, r) in enumerate(clumps):
        blob("crown_%d" % k, r, (x, y, z), "hx_leaf_dk" if k % 3 == 0 else "hx_leaf", (1.0, 0.85, 0.55))
        if k % 2:
            blob("crown_rust_%d" % k, r * 0.5, (x + 0.25, y - 0.35, z + 0.15), "hx_leaf_rust", (1.0, 0.8, 0.5))
    # The moss, hanging from the underside of the crown.
    for k, (x, z, length) in enumerate(((-2.0, 3.55, 0.9), (-1.2, 3.5, 1.3), (2.0, 3.7, 1.0), (1.1, 3.7, 1.4),
                                         (-0.45, 3.55, 0.8), (0.4, 3.6, 1.1))):
        blk("moss_%d" % k, (0.14, 0.05, length), (x, -0.75, z - length / 2), "hx_moss" if k % 2 else "hx_moss_dk",
            bev=0)
        blk("moss_t_%d" % k, (0.08, 0.05, length * 0.4), (x + 0.07, -0.76, z - length - 0.1), "hx_moss", bev=0)
    return 5.8


def prop_cypress_knees():
    """The knobs a cypress sends up out of the mud round it."""
    for k, (x, y, r, h) in enumerate(((0, 0, 0.18, 0.7), (0.4, 0.15, 0.14, 0.5), (-0.38, 0.2, 0.15, 0.55),
                                      (0.15, -0.3, 0.12, 0.4), (-0.2, -0.28, 0.1, 0.32))):
        frustum("knee_%d" % k, r * 1.3, r * 0.6, h, (x, y, h / 2), "hx_bark" if k % 2 else "hx_bark_lt", verts=8)
        blob("knob_%d" % k, r * 0.7, (x, y, h), "hx_bark_lt", (1, 1, 0.8))
    blob("mud", 0.7, (0, 0, 0.0), "hx_mud", (1.0, 0.7, 0.12))
    return 1.5


def prop_hex_lantern():
    """A crooked pole with an arm off the top and a jar of the cult's green
    light hung from it: what lights the ways through the Hexmire at night."""
    cyl("pole", 0.09, 2.2, (0, 0, 1.1), "hx_post", verts=7, rot=(0, 0.04, 0))
    blk("arm", (0.7, 0.07, 0.07), (-0.3, 0, 2.15), "hx_post", bev=0)
    blk("cord", (0.03, 0.03, 0.3), (-0.58, 0, 2.0), "hx_rope", bev=0)
    jar("jar", -0.58, 0, 1.72, s=1.6, emit=1.6)
    blk("rag", (0.1, 0.03, 0.4), (0.1, -0.08, 1.8), "hx_red", bev=0)
    skull("skull", 0.0, -0.1, 1.35, 0.7)
    blob("base", 0.3, (0, 0, 0.0), "hx_mud", (1, 1, 0.3))
    return 2.6


# =================================================================================
#  Shellback Strand
# =================================================================================

def _scute(name, centre, normal, size, colour, ring_colour):
    """One plate of a shell, laid on a dome: a flat six-sided plate facing out
    along `normal`, and a paler one inset on it -- the growth rings -- so each
    reads as a plate and the dark of the dome between them as the seams."""
    n = Vector(normal).normalized()
    rot = Vector((0, 0, 1)).rotation_difference(n).to_euler()
    cyl(name, size, 0.14, centre, colour, verts=6, rot=rot)
    inner = Vector(centre) + n * 0.06
    cyl(name + "_ring", size * 0.58, 0.1, tuple(inner), ring_colour, verts=6, rot=rot)


def prop_shellback_hut():
    """A Shellback's house: a low round wall of mud and driftwood, roofed with a
    dome laid in plates like the shell of the people who live in it -- a plate
    on the crown, a ring of plates round it, a ring below, and the small plates
    of the rim -- a round door, dark, and shells hung either side of it."""
    cyl("wall", 2.0, 1.0, (0, 0, 0.5), "hx_mud", verts=24)
    for k in range(14):
        a = k / 14 * math.tau
        blk("drift_%d" % k, (0.34, 0.12, 0.9), (math.cos(a) * 2.02, math.sin(a) * 2.02, 0.5), "hx_drift",
            rot=(0, 0, a + math.pi / 2), bev=0.02)
    R, z0, squash = 2.15, 1.0, 0.72
    blob("dome", R, (0, 0, z0), "hx_shell_dk", (1.0, 1.0, squash))
    def on_dome(az, el):
        c = (math.cos(el) * math.cos(az) * R, math.cos(el) * math.sin(az) * R, z0 + math.sin(el) * R * squash)
        nrm = (math.cos(el) * math.cos(az) * squash, math.cos(el) * math.sin(az) * squash, math.sin(el))
        return c, nrm
    c, nrm = on_dome(0, math.pi / 2)
    _scute("crown", c, nrm, 0.66, "hx_shell", "hx_shell_lt")
    for ring, (n, el, size, turn) in enumerate(((6, 0.98, 0.64, 0.0), (8, 0.52, 0.72, 0.39), (13, 0.10, 0.48, 0.1))):
        for k in range(n):
            az = k / n * math.tau + turn
            c, nrm = on_dome(az, el)
            _scute("scute_%d_%d" % (ring, k), c, nrm, size, "hx_shell" if ring < 2 else "hx_shell_lt",
                   "hx_shell_lt" if ring < 2 else "hx_hide")
    # The door: a round opening on the front, through the wall and the rim.
    cyl("door_rim", 0.66, 0.3, (0, -2.02, 0.66), "hx_drift_dk", verts=16, rot=(math.pi / 2, 0, 0))
    cyl("door", 0.54, 0.34, (0, -2.08, 0.66), "hx_void", verts=16, rot=(math.pi / 2, 0, 0))
    for s in (-1, 1):
        blk("cord_%d" % s, (0.03, 0.03, 0.5), (s * 1.0, -2.1, 1.2), "hx_rope", bev=0)
        blob("hang_shell_%d" % s, 0.16, (s * 1.0, -2.12, 0.9), "hx_conch", (0.9, 0.5, 1.2))
        blob("hang_in_%d" % s, 0.08, (s * 1.0, -2.2, 0.9), "hx_conch_in", (0.9, 0.5, 1.2))
    return 5.0


def prop_shell_midden():
    """A heap of what the Shellbacks have eaten: big clam shells, a conch, and
    fish bones, on a mound of broken shell."""
    blob("mound", 0.8, (0, 0, 0.05), "hx_shell_lt", (1.0, 0.7, 0.35))
    for k, (x, y, z, a) in enumerate(((-0.35, -0.1, 0.3, 0.4), (0.3, -0.2, 0.28, -0.5), (0.05, 0.2, 0.42, 0.1),
                                      (-0.1, -0.38, 0.2, 0.9))):
        blob("clam_%d" % k, 0.26, (x, y, z), "hx_conch" if k % 2 else "hx_hide", (1.0, 0.4, 0.8)).rotation_euler = (a, 0, a)
    frustum("conch", 0.2, 0.02, 0.6, (0.45, 0.05, 0.4), "hx_conch", rot=(0, 1.2, 0.3), verts=10)
    blob("conch_in", 0.1, (0.28, -0.05, 0.36), "hx_conch_in", (1, 0.6, 1))
    rod("spine", (-0.6, -0.1, 0.15), (-0.1, -0.35, 0.22), 0.03, "hx_bone")
    for k in range(4):
        rod("rib_%d" % k, (-0.5 + k * 0.12, -0.14 - k * 0.05, 0.16), (-0.45 + k * 0.12, -0.3 - k * 0.05, 0.08), 0.02,
            "hx_bone")
    return 1.9


def prop_driftwood():
    """A bleached log washed up, the pale end of it cut, the stub of a root
    sticking up at the other, and weed caught on it."""
    rod("log", (-0.85, 0.1, 0.2), (0.75, -0.1, 0.22), 0.2, "hx_drift", verts=10)
    cyl("cut", 0.19, 0.04, (-0.87, 0.1, 0.2), "hx_hide", verts=10, rot=(0, math.pi / 2, 0))
    rod("stub", (0.7, -0.1, 0.24), (1.0, -0.25, 0.6), 0.12, "hx_drift_dk")
    rod("stub2", (0.72, -0.05, 0.2), (1.02, 0.2, 0.42), 0.1, "hx_drift_dk")
    rod("branch", (-0.2, 0.0, 0.3), (-0.45, -0.35, 0.6), 0.07, "hx_drift")
    for k in range(3):
        blk("grain_%d" % k, (1.1, 0.03, 0.03), (-0.1, -0.2, 0.18 + k * 0.08), "hx_drift_dk", bev=0)
    blk("weed", (0.4, 0.05, 0.14), (0.25, -0.22, 0.3), "hx_kelp", rot=(0, 0.2, 0.1), bev=0)
    return 2.3


# =================================================================================
#  The Candle Fens
# =================================================================================

def prop_fetish_pole():
    """A pole planted by the cult: a skull on the top with feathers stood up
    behind it, strings of bones and bottles hung off it, rags tied round, and
    a band of red and a band of chalk."""
    cyl("pole", 0.13, 3.0, (0, 0, 1.5), "hx_post", verts=8)
    cyl("band_r", 0.15, 0.3, (0, 0, 1.0), "hx_red", verts=8)
    cyl("band_w", 0.15, 0.14, (0, 0, 1.5), "hx_chalk", verts=8)
    cyl("band_r2", 0.15, 0.14, (0, 0, 2.2), "hx_red", verts=8)
    skull("top", 0, -0.05, 3.18, 1.4, eyes="hx_glow", emit=1.3)
    for k, a in enumerate((-0.6, -0.25, 0.1, 0.45)):
        blk("plume_%d" % k, (0.1, 0.03, 0.7), (math.sin(a) * 0.3, 0.12, 3.55), "hx_feather" if k % 2 else "hx_red",
            rot=(0, a, 0), bev=0)
    blk("cross", (1.0, 0.1, 0.1), (0, 0, 2.6), "hx_post_lt", bev=0)
    for s in (-1, 1):
        blk("string_%d" % s, (0.03, 0.03, 0.6), (s * 0.45, -0.04, 2.28), "hx_rope", bev=0)
        if s < 0:
            for k in range(3):
                blob("bead_%d" % k, 0.06, (s * 0.45, -0.05, 2.35 - k * 0.14), "hx_bone", (1, 1, 1.3))
            blob("jaw", 0.12, (s * 0.45, -0.06, 1.9), "hx_bone_dk", (1.2, 0.6, 0.7))
        else:
            bottle("bottle", s * 0.45, -0.05, 1.9, "hx_bottle_g", s=1.2)
    blk("rag", (0.34, 0.03, 0.44), (0.1, -0.14, 1.28), "hx_cloth", rot=(0, 0.15, 0), bev=0)
    blob("foot", 0.3, (0, 0, 0.0), "hx_mud", (1, 1, 0.3))
    return 4.0


def prop_candle_shrine():
    """A little shrine on the ground: a low box of planks with a cloth over it,
    candles on it and round it, a skull at the back, bottles, and a doll."""
    blk("box", (1.0, 0.5, 0.4), (0, 0.05, 0.2), "hx_post_lt", bev=0.02)
    blk("cloth", (1.04, 0.54, 0.06), (0, 0.05, 0.42), "hx_red", bev=0)
    blk("cloth_hang", (0.9, 0.04, 0.2), (0, -0.21, 0.32), "hx_red_dk", bev=0)
    skull("skull", 0, 0.12, 0.62, 1.0)
    for k, (x, y, h) in enumerate(((-0.38, -0.05, 0.22), (-0.22, 0.12, 0.3), (0.3, 0.1, 0.26), (0.4, -0.1, 0.18))):
        candle("c_%d" % k, x, y, 0.45, h=h)
    for k, (x, y) in enumerate(((-0.62, -0.35), (0.6, -0.3), (-0.15, -0.5), (0.2, -0.52))):
        candle("g_%d" % k, x, y, 0.0, h=0.16 + 0.04 * (k % 2), r=0.045)
    bottle("b1", 0.18, -0.08, 0.57, "hx_bottle_a", s=0.9)
    bottle("b2", -0.1, -0.12, 0.55, "hx_bottle_v", s=0.8)
    # A doll of sacking, sat against the skull.
    blk("doll", (0.14, 0.08, 0.18), (0.14, -0.02, 0.55), "hx_cloth", bev=0.02)
    blob("doll_head", 0.07, (0.14, -0.02, 0.69), "hx_cloth")
    blk("doll_pin", (0.02, 0.1, 0.02), (0.14, -0.08, 0.57), "hx_chalk", bev=0)
    return 1.6


def prop_hex_drum():
    """A tall drum: a hollowed log, hide stretched over the top and pegged, a
    band of red round the middle and chalk marks on it."""
    frustum("body", 0.36, 0.44, 1.1, (0, 0, 0.55), "hx_post_lt", verts=14)
    cyl("skin", 0.46, 0.06, (0, 0, 1.12), "hx_hide", verts=14)
    cyl("band", 0.41, 0.16, (0, 0, 0.55), "hx_red", verts=14)
    for k in range(8):
        a = k / 8 * math.tau
        blk("peg_%d" % k, (0.06, 0.06, 0.16), (math.cos(a) * 0.45, math.sin(a) * 0.45, 0.98), "hx_bone_dk", bev=0)
    for k in range(3):
        blk("mark_%d" % k, (0.08, 0.02, 0.08), ((k - 1) * 0.16, -0.41, 0.8), "hx_chalk", bev=0)
    frustum("foot", 0.46, 0.4, 0.12, (0, 0, 0.06), "hx_post", verts=14)
    return 1.4


def prop_bottle_tree():
    """A dead tree with a bottle pushed onto the end of every branch, blue and
    green and amber, to catch whatever walks at night."""
    frustum("trunk", 0.26, 0.12, 2.4, (0, 0, 1.2), "hx_bark_dk", verts=9)
    branches = [((0, 0, 1.6), (-1.0, 0.0, 2.5)), ((0, 0, 1.9), (0.95, -0.1, 2.7)), ((0, 0, 2.3), (-0.35, 0.1, 3.3)),
                ((0, 0, 2.2), (0.45, 0.1, 3.4)), ((-0.6, 0, 2.15), (-1.35, -0.1, 2.3)),
                ((0.6, -0.05, 2.4), (1.4, -0.1, 2.35))]
    colours = ["hx_bottle_b", "hx_bottle_g", "hx_bottle_b", "hx_bottle_a", "hx_bottle_g", "hx_bottle_b"]
    for k, (p0, p1) in enumerate(branches):
        rod("branch_%d" % k, p0, p1, 0.07, "hx_bark_dk")
        dx, dz = p1[0] - p0[0], p1[2] - p0[2]
        tilt = math.atan2(dx, dz)
        bottle("bottle_%d" % k, p1[0] + math.sin(tilt) * 0.12, p1[1] - 0.02, p1[2] + math.cos(tilt) * 0.12,
               colours[k], s=1.5, rot=(0, tilt + math.pi, 0), emit=0.9)
    blob("roots", 0.5, (0, 0, 0.0), "hx_mud", (1, 1, 0.3))
    return 3.9


# =================================================================================
#  The temple's stockade
# =================================================================================

def _stakes(prefix, n, along_x, length, h=2.6):
    for k in range(n):
        t = (k + 0.5) / n - 0.5
        x, y = (t * length, 0.0) if along_x else (0.0, t * length)
        hh = h + (0.25 if k % 3 == 0 else 0.0) - (0.15 if k % 4 == 1 else 0.0)
        stake("%s_%d" % (prefix, k), x, y, hh, r=0.2, colour="hx_post" if k % 2 else "hx_post_lt")


def prop_stockade_wall():
    """Four cells of the stockade, running across: sharpened stakes lashed with
    two bands of rope, a skull tied on every so often, a painted band."""
    _stakes("stake", 10, True, 4.0)
    for z in (0.7, 1.9):
        blk("lash_%s" % z, (4.0, 0.46, 0.1), (0, 0, z), "hx_rope", bev=0)
    blk("paint", (4.0, 0.44, 0.22), (0, -0.01, 1.2), "hx_red", bev=0)
    for k in (-1, 1):
        skull("skull_%d" % k, k * 1.0, -0.28, 1.6, 1.3)
    return (4.4, BUILDING)


def prop_stockade_wall_v():
    """The same, running up the screen."""
    _stakes("stake", 10, False, 4.0)
    for z in (0.7, 1.9):
        blk("lash_%s" % z, (0.46, 4.0, 0.1), (0, 0, z), "hx_rope", bev=0)
    blk("paint", (0.44, 4.0, 0.22), (0, 0, 1.2), "hx_red", bev=0)
    return (3.0, BUILDING)


def prop_stockade_gate():
    """The way into the temple's yard: two posts taller than the stockade, a
    lintel across them with skulls along it, green jars either side, and the
    way through open."""
    for s in (-1, 1):
        x = s * 1.9
        cyl("post_%d" % s, 0.3, 4.2, (x, 0, 2.1), "hx_post", verts=9)
        cone("tip_%d" % s, 0.32, 0.6, (x, 0, 4.5), "hx_bone_dk", verts=9)
        cyl("band_%d" % s, 0.32, 0.34, (x, 0, 1.5), "hx_red", verts=9)
        cyl("band2_%d" % s, 0.32, 0.16, (x, 0, 2.3), "hx_chalk", verts=9)
        for k in range(3):
            stake("side_%d_%d" % (s, k), x + s * (0.55 + k * 0.4), 0, 2.6 + (0.2 if k == 1 else 0.0), r=0.2)
        blk("bracket_%d" % s, (0.5, 0.08, 0.08), (x - s * 0.35, -0.25, 3.3), "hx_post", bev=0)
        jar("jar_%d" % s, x - s * 0.55, -0.25, 2.95, s=1.5)
    blk("lintel", (4.5, 0.4, 0.4), (0, 0, 3.8), "hx_post_lt", bev=0.03)
    blk("lintel_paint", (4.52, 0.42, 0.12), (0, 0, 3.66), "hx_red", bev=0)
    for k in range(5):
        skull("lskull_%d" % k, -1.4 + k * 0.7, -0.28, 4.18, 1.1, eyes="hx_glow" if k == 2 else "hx_void",
              emit=1.4 if k == 2 else 0.0)
    return (6.9, BUILDING)


# =================================================================================
#  The temple, and its sanctum
# =================================================================================

def prop_hex_temple():
    """The cult's temple: a long house of ochre daub and black posts on a mound
    of packed earth, under a great steep roof of thatch that comes down low
    over the walls; posts painted red and chalk along the front, a skull over
    the door with a green light in its eyes, the door a dark way in on its own
    steps, fetish poles either side of it and a smaller roof over the porch."""
    W, D, H = 9.0, 5.0, 3.0
    front = -D / 2
    blk("mound", (W + 1.4, D + 1.2, 0.6), (0, 0, 0.3), "hx_mud", bev=0.05)
    blk("walls", (W, D, H), (0, 0, 0.6 + H / 2), "hx_daub", bev=0.03)
    blk("sill", (W + 0.1, D + 0.1, 0.2), (0, 0, 0.7), "hx_post", bev=0.01)
    blk("wall_band", (W + 0.04, D + 0.04, 0.3), (0, 0, 0.6 + H * 0.62), "hx_red", bev=0)
    for k in range(9):
        x = -W / 2 + 0.3 + k * (W - 0.6) / 8
        blk("post_%d" % k, (0.3, 0.3, H + 0.3), (x, front - 0.05, 0.6 + (H + 0.3) / 2), "hx_post", bev=0.02)
        blk("post_mark_%d" % k, (0.32, 0.32, 0.12), (x, front - 0.05, 0.6 + H * 0.35),
            "hx_chalk" if k % 2 else "hx_red", bev=0)
    for s in (-1, 1):
        blk("win_%d" % s, (0.8, 0.06, 0.6), (s * 3.0, front - 0.03, 0.6 + H * 0.6), "hx_void", bev=0)
        blk("win_glow_%d" % s, (0.5, 0.06, 0.34), (s * 3.0, front - 0.04, 0.6 + H * 0.56), "hx_glow", emit=1.0, bev=0)
    # The roof: two slopes of thatch meeting at a ridge, the front one's hem
    # ragged and short enough that the painted posts show under it; courses
    # laid proud of the thatch so it reads as straw in rows, not a board.
    RH, over = 2.8, 0.35
    slope = math.atan2(RH, D / 2 + over)
    length = math.hypot(RH, D / 2 + over)
    for s in (-1, 1):
        cy, cz = s * (D / 2 + over) / 2, 0.6 + H + RH / 2 - 0.1
        blk("roof_%d" % s, (W + 1.2, length, 0.34), (0, cy, cz), "hx_thatch", rot=(-s * slope, 0, 0), bev=0.04)
        # Along the slope's outward normal, a little proud of it.
        ny, nz = s * math.sin(slope), math.cos(slope)
        for k in range(5):
            t = (k + 0.5) / 5
            y = s * (D / 2 + over) * (1 - t) + ny * 0.2
            z = 0.6 + H - 0.1 + RH * t + nz * 0.2
            blk("course_%d_%d" % (s, k), (W + 1.24, 0.12, 0.08), (0, y, z), "hx_thatch_dk", rot=(-s * slope, 0, 0),
                bev=0)
            # Straws: thin streaks down the slope, many and uneven, some darker
            # and some paler -- a regular row of them reads as brickwork.
            for j in range(30):
                h = ((j * 37 + k * 11 + (s + 1) * 5) % 17) / 17.0
                x = -W / 2 - 0.4 + (j + h * 0.8) * (W + 0.8) / 30
                ts = t - 0.06 - 0.06 * h
                blk("straw_%d_%d_%d" % (s, k, j), (0.045, length / 5 * (0.35 + 0.45 * h), 0.05),
                    (x, s * (D / 2 + over) * (1 - ts) + ny * 0.19, 0.6 + H - 0.1 + RH * ts + nz * 0.19),
                    "hx_thatch_dk" if (j + k) % 3 else "hx_rope", rot=(-s * slope, 0, 0), bev=0)
        # A ragged hem: tufts hanging past the eaves.
        for k in range(14):
            x = -W / 2 - 0.4 + k * (W + 0.8) / 13
            blk("hem_%d_%d" % (s, k), (0.5, 0.2, 0.3 + 0.12 * (k % 3)), (x, s * (D / 2 + over + 0.05), 0.6 + H - 0.2),
                "hx_thatch_dk" if k % 2 else "hx_thatch", bev=0.02)
    blk("ridge", (W + 1.4, 0.6, 0.4), (0, 0, 0.6 + H + RH + 0.05), "hx_thatch_dk", bev=0.05)
    for k in range(7):
        x = -W / 2 + 0.6 + k * (W - 1.2) / 6
        rod("ridge_x_%d" % k, (x - 0.35, -0.3, 0.6 + H + RH - 0.1), (x + 0.35, 0.3, 0.6 + H + RH + 0.55), 0.06,
            "hx_post")
    for s in (-1, 1):
        # The gable ends, closed with daub under the thatch.
        for k in range(5):
            t = k / 5
            blk("gable_%d_%d" % (s, k), (0.2, D * (1 - t) * 0.98, RH / 5),
                (s * (W / 2 - 0.1), 0, 0.6 + H + RH * (t + 0.1)), "hx_daub_dk", bev=0)
        cone("finial_%d" % s, 0.2, 1.2, (s * (W / 2 + 0.5), 0, 0.6 + H + RH + 0.7), "hx_post", verts=6)
        skull("finial_skull_%d" % s, s * (W / 2 + 0.5), -0.1, 0.6 + H + RH + 0.35, 1.3)
    # The porch: a little roof on two posts over the door, and the door.
    py = front - 1.0
    for s in (-1, 1):
        blk("porch_post_%d" % s, (0.28, 0.28, 2.6), (s * 1.2, py - 0.6, 0.6 + 1.3), "hx_post", bev=0.02)
        blk("porch_band_%d" % s, (0.3, 0.3, 0.3), (s * 1.2, py - 0.6, 0.6 + 1.0), "hx_red", bev=0)
    for s in (-1, 1):
        blk("porch_roof_%d" % s, (1.8, 2.4, 0.24), (s * 0.8, py - 0.1, 0.6 + 2.95), "hx_thatch",
            rot=(0, s * 0.55, 0), bev=0.04)
        blk("porch_hem_%d" % s, (1.8, 0.2, 0.3), (s * 0.8, py - 1.3, 0.6 + 2.85), "hx_thatch_dk",
            rot=(0, s * 0.55, 0), bev=0.02)
    blk("porch_ridge", (0.3, 2.5, 0.3), (0, py - 0.1, 0.6 + 3.45), "hx_thatch_dk", bev=0.03)
    blk("porch_gable", (2.2, 0.1, 0.5), (0, py - 1.25, 0.6 + 2.75), "hx_red", bev=0)
    skull("porch_skull", 0, py - 1.35, 0.6 + 3.1, 1.2, eyes="hx_glow", emit=1.5)
    blk("door", (1.3, 0.1, 2.0), (0, front - 0.06, 0.6 + 1.0), "hx_void", bev=0)
    blk("door_frame", (1.6, 0.08, 2.2), (0, front - 0.04, 0.6 + 1.1), "hx_post", bev=0)
    for k in range(3):
        blk("step_%d" % k, (2.2 - k * 0.3, 0.45, 0.2), (0, front - 0.3 - (2 - k) * 0.4, 0.1 + k * 0.2), "hx_mud")
    # Fetish poles either side, and green jars on the porch posts.
    for s in (-1, 1):
        x = s * 2.3
        cyl("pole_%d" % s, 0.12, 3.4, (x, front - 1.6, 1.7), "hx_post", verts=8)
        cyl("pole_band_%d" % s, 0.14, 0.3, (x, front - 1.6, 1.2), "hx_red", verts=8)
        skull("pole_skull_%d" % s, x, front - 1.65, 3.55, 1.2, eyes="hx_glow", emit=1.2)
        for k, a in enumerate((-0.5, 0.0, 0.5)):
            blk("pole_plume_%d_%d" % (s, k), (0.1, 0.03, 0.6), (x + math.sin(a) * 0.25, front - 1.5, 3.9),
                "hx_feather" if k != 1 else "hx_red", rot=(0, a, 0), bev=0)
        jar("porch_jar_%d" % s, s * 1.2, py - 0.8, 0.6 + 2.0, s=1.4)
    return (12.0, BUILDING)


def prop_poto_mitan():
    """The post at the middle of the sanctum that everything turns round:
    painted in a spiral of red and chalk, on a round plinth of painted clay,
    candles round its foot."""
    cyl("plinth", 0.7, 0.3, (0, 0, 0.15), "hx_daub_dk", verts=16)
    cyl("plinth_band", 0.72, 0.08, (0, 0, 0.26), "hx_red", verts=16)
    cyl("post", 0.2, 3.2, (0, 0, 1.9), "hx_post_lt", verts=10)
    for k in range(10):
        z = 0.5 + k * 0.3
        a = k * 0.9
        blk("spiral_%d" % k, (0.44, 0.44, 0.1), (0, 0, z), "hx_red" if k % 2 else "hx_chalk", rot=(0.25, 0, a),
            bev=0)
    skull("top", 0, -0.05, 3.62, 1.2, eyes="hx_glow", emit=1.3)
    for k in range(6):
        a = k / 6 * math.tau + 0.4
        candle("c_%d" % k, math.cos(a) * 0.5, math.sin(a) * 0.5, 0.3, h=0.2, r=0.05)
    return 4.1


def prop_hex_altar():
    """The High Priest's altar: a long table under a red cloth, a skull with
    horns at its middle and candles all along it, bottles, a bowl, and the
    poppets sat along its edge."""
    blk("top", (2.6, 0.8, 0.14), (0, 0, 0.82), "hx_post_lt", bev=0.02)
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.14, 0.14, 0.76), (sx * 1.1, sy * 0.28, 0.38), "hx_post", bev=0)
    blk("cloth", (2.64, 0.84, 0.05), (0, 0, 0.9), "hx_red", bev=0)
    blk("cloth_front", (2.3, 0.04, 0.5), (0, -0.42, 0.66), "hx_red_dk", bev=0)
    for k in range(4):
        blk("fringe_%d" % k, (0.1, 0.04, 0.16), (-0.9 + k * 0.6, -0.43, 0.36), "hx_chalk", bev=0)
    blob("skull", 0.24, (0, 0.1, 1.15), "hx_bone", (1.1, 0.95, 1.0))
    blk("skull_jaw", (0.26, 0.2, 0.12), (0, 0.06, 0.97), "hx_bone_dk", bev=0.01)
    for s in (-1, 1):
        blk("skull_eye_%d" % s, (0.08, 0.04, 0.08), (s * 0.09, -0.13, 1.17), "hx_glow", emit=1.5, bev=0)
        cone("skull_horn_%d" % s, 0.07, 0.6, (s * 0.35, 0.1, 1.35), "hx_bone", verts=6, rot=(0, s * 1.0, 0))
    for k, x in enumerate((-1.15, -0.85, -0.5, 0.5, 0.85, 1.15)):
        candle("c_%d" % k, x, 0.12 - 0.1 * (k % 2), 0.92, h=0.2 + 0.1 * (k % 3), r=0.05)
    bottle("b1", -0.3, 0.2, 1.05, "hx_bottle_v")
    bottle("b2", 0.32, 0.22, 1.02, "hx_bottle_g", s=0.9)
    cyl("bowl", 0.16, 0.08, (0.62, -0.1, 0.96), "hx_bone_dk", verts=10)
    blk("bowl_in", (0.2, 0.2, 0.02), (0.62, -0.1, 1.0), "hx_red_dk", bev=0)
    for k, x in enumerate((-0.7, -0.25, 0.25)):
        blk("doll_%d" % k, (0.12, 0.07, 0.16), (x, -0.28, 1.0), "hx_cloth" if k % 2 else "hx_hide", bev=0.02)
        blob("doll_head_%d" % k, 0.065, (x, -0.28, 1.13), "hx_cloth" if k % 2 else "hx_hide")
        blk("doll_pin_%d" % k, (0.02, 0.1, 0.02), (x + 0.02, -0.34, 1.02), "hx_chalk", bev=0)
    return 3.2


# =================================================================================
#  The High Priest's totem
# =================================================================================

def prop_totem_hex_priest():
    """The High Priest's totem: a black cypress post banded in green light,
    with a poppet of sacking lashed to its front and stuck through with pins,
    and a horned skull on top."""
    r = bp._totem("tt_hex", "tt_hex_b", "skull", eye_emit=1.7)
    blk("poppet", (0.2, 0.1, 0.26), (0, -0.2, 0.52), "hx_cloth", bev=0.02)
    blob("poppet_head", 0.1, (0, -0.2, 0.72), "hx_cloth")
    for k, (x, z) in enumerate(((-0.05, 0.56), (0.06, 0.48), (0.02, 0.74))):
        blk("pin_%d" % k, (0.02, 0.16, 0.02), (x, -0.3, z), "hx_chalk", bev=0)
    for s in (-1, 1):
        cone("horn_%d" % s, 0.05, 0.3, (s * 0.2, 0, 1.24), "hx_bone", verts=6, rot=(0, s * 0.8, 0))
    return r


# =================================================================================
#  Registration: name -> (builder, final pixels across)
# =================================================================================
PROPS = {
    "hex_gateway":     (prop_hex_gateway, 230),
    "cypress_tree":    (prop_cypress_tree, 186),
    "cypress_knees":   (prop_cypress_knees, 48),
    "hex_lantern":     (prop_hex_lantern, 84),
    "shellback_hut":   (prop_shellback_hut, 160),
    "shell_midden":    (prop_shell_midden, 60),
    "driftwood":       (prop_driftwood, 84),
    "fetish_pole":     (prop_fetish_pole, 128),
    "candle_shrine":   (prop_candle_shrine, 52),
    "hex_drum":        (prop_hex_drum, 46),
    "bottle_tree":     (prop_bottle_tree, 124),
    "stockade_wall":   (prop_stockade_wall, 144),
    "stockade_wall_v": (prop_stockade_wall_v, 96),
    "stockade_gate":   (prop_stockade_gate, 220),
    "hex_temple":      (prop_hex_temple, 384),
    "poto_mitan":      (prop_poto_mitan, 132),
    "hex_altar":       (prop_hex_altar, 104),
    "totem_hex_priest": (prop_totem_hex_priest, 32),
}
