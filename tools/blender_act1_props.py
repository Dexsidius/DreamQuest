# =============================================================================
#  blender_act1_props.py - what Act I's scenes stand on and in: the Reverie's
#  knots of black thread (the Nightmare Holds, the mini anchors, the colossal
#  Anchor round the town well, the barrier round the square), the Dawn Bells
#  and Chimes, Halda's chained chair and her dream forge, the dream tannery,
#  Bess's cellar and inn, the Tanner's yard, the Black Knights' scrap, the
#  Mayor's Hall in the dream and the Guild master's desk.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only nightmare_hold,mini_anchor
#
#  Built with blender_props.py's own tools and the prologue's, and registered
#  into blender_props.PROPS; that file hands itself over (see where PROPS is
#  assembled there). Sizes that are not square are cut by make_props.ps1
#  ($CROP); decals and things in the air throw no contact shadow ($NOSHADOW);
#  the square's barrier tiles side by side ($TILE_X). The dragon's shadow is
#  not here: it is soft-edged, which a render cut to on-or-off alpha cannot
#  be, and is drawn by tools/make_act1_overlays.ps1.
# =============================================================================

import math
import random

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

import blender_props as bp
import blender_prologue_props as pp

blk, cyl, cone, sphere, material, turn_all = bp.blk, bp.cyl, bp.cone, bp.sphere, bp.material, bp.turn_all
ring, limb, centre, tip_all = pp.ring, pp.limb, pp.centre, pp.tip_all

bp.PALETTE.update({
    # The Reverie's thread: black with a violet sheen where the light runs
    # along it, and the dream smoke it sits in.
    "thread":      (0.110, 0.090, 0.140), "thread_lt":   (0.230, 0.190, 0.310),
    "thread_sheen": (0.420, 0.330, 0.580), "dsmoke":     (0.260, 0.200, 0.360),
    "dsmoke_lt":   (0.380, 0.300, 0.500), "dream_pale":  (0.880, 0.880, 0.980),
    "dream_pale_dk": (0.700, 0.700, 0.860),
})


# --- helpers ------------------------------------------------------------------------
def knot(prefix, centre_, count, radius, thick, seed, squash=(1.0, 1.0, 1.0), spread=0.06,
         colours=("thread", "thread_lt", "thread", "thread_sheen")):
    """A knot of black thread: loops of it every which way round a centre,
    squashed to the shape asked for. Deterministic: the same seed winds the
    same knot."""
    rnd = random.Random(seed)
    cx, cy, cz = centre_
    out = []
    for k in range(count):
        major = rnd.uniform(radius[0], radius[1])
        minor = rnd.uniform(thick[0], thick[1])
        rot = (math.radians(rnd.uniform(-80, 80)), math.radians(rnd.uniform(-80, 80)),
               math.radians(rnd.uniform(0, 180)))
        loc = (cx + rnd.uniform(-spread, spread), cy + rnd.uniform(-spread, spread),
               cz + rnd.uniform(-spread, spread) * 1.4)
        ob = ring("%s_%d" % (prefix, k), major, minor, loc, colours[k % len(colours)], rot=rot, rough=0.35)
        # Pulled long one way and narrow the other: perfect circles made a
        # birdcage of hoops, not a knot.
        ob.scale = (squash[0] * rnd.uniform(0.95, 1.25), squash[1] * rnd.uniform(0.45, 0.85), squash[2])
        # Squashing a loop about its own centre leaves it where it was; the
        # knot is squashed about the knot's.
        ob.location = (cx + (loc[0] - cx) * squash[0], cy + (loc[1] - cy) * squash[1],
                       cz + (loc[2] - cz) * squash[2])
        out.append(ob)
    return out


def puffs(prefix, spots, colours=("dsmoke", "dsmoke_lt")):
    """Dream smoke: flattened balls run together, (x, y, z, rx, ry, rz) each."""
    for k, (x, y, z, rx, ry, rz) in enumerate(spots):
        s = sphere("%s_%d" % (prefix, k), 1.0, (x, y, z), colours[k % len(colours)], rough=0.9)
        s.scale = (rx, ry, rz)


def strand(name, a, b, r0, r1=None, colour="thread", sag=0.0, segs=3):
    """A cord from a to b, sagging by `sag` in the middle, as a few tapered
    lengths."""
    a, b = Vector(a), Vector(b)
    r1 = r0 * 0.5 if r1 is None else r1
    pts = []
    for i in range(segs + 1):
        f = i / segs
        p = a.lerp(b, f)
        p.z -= sag * 4.0 * f * (1.0 - f)
        pts.append(p)
    for i in range(segs):
        f0, f1 = i / segs, (i + 1) / segs
        limb("%s_%d" % (name, i), pts[i], pts[i + 1], r0 + (r1 - r0) * f0, r0 + (r1 - r0) * f1, colour)


def lathe(name, profile, loc, colour, emit=0.0, rough=0.5, metal=0.0, segments=32, rot=(0, 0, 0)):
    """A solid turned about Z from (radius, z) points listed top to bottom:
    a bell, a mug, a vat."""
    bm = bmesh.new()
    rings = []
    for (r_, z) in profile:
        rings.append([bm.verts.new((r_ * math.cos(i / segments * math.tau), r_ * math.sin(i / segments * math.tau), z))
                      for i in range(segments)])
    for r0, r1 in zip(rings, rings[1:]):
        for i in range(segments):
            j = (i + 1) % segments
            bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    for p in me.polygons:
        p.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    ob.location = loc
    ob.rotation_euler = rot
    ob.data.materials.append(material(name, colour, rough, metal, emit))
    return ob


def scale_all(k, pivot=(0, 0, 0)):
    """Everything built so far, scaled about `pivot`."""
    p = bpy.data.objects.new("scale_pivot", None)
    bpy.context.collection.objects.link(p)
    p.location = pivot
    for ob in list(bpy.context.scene.objects):
        if ob is p or ob.parent is not None or ob.type not in {"MESH", "EMPTY"}:
            continue
        ob.parent = p
        ob.location = ob.location - Vector(pivot)
    p.scale = (k, k, k)
    return p


# =================================================================================
#  Scene 20 -- the Nightmare Holds
# =================================================================================
def _dream_self(cx, cy, cz, s=1.0):
    """A sleeper's dream-self curled up as if in the womb: pale and glowing a
    little, the head down on the drawn-up knees, the arms round them."""
    e = 1.3
    sphere("ds_head", 0.115 * s, (cx - 0.02 * s, cy - 0.06 * s, cz + 0.15 * s), "dream_pale", emit=e)
    back = sphere("ds_back", 0.17 * s, (cx + 0.04 * s, cy + 0.05 * s, cz + 0.02 * s), "dream_pale", emit=e)
    back.scale = (0.9, 1.0, 1.15)
    knees = sphere("ds_knees", 0.12 * s, (cx - 0.03 * s, cy - 0.12 * s, cz - 0.05 * s), "dream_pale_dk", emit=e)
    knees.scale = (1.2, 0.9, 1.0)
    sphere("ds_feet", 0.07 * s, (cx + 0.06 * s, cy - 0.04 * s, cz - 0.17 * s), "dream_pale_dk", emit=e)
    for sx in (-1, 1):
        limb("ds_arm_%d" % sx, (cx + sx * 0.12 * s, cy + 0.0, cz + 0.10 * s),
             (cx + sx * 0.08 * s, cy - 0.18 * s, cz - 0.02 * s), 0.045 * s, 0.035 * s, "dream_pale", verts=8)


def prop_nightmare_hold():
    """A Nightmare Hold: a knot of black thread and dream smoke hanging a little
    off the ground, wound round the curled-up dream-self of a sleeper who glows
    pale through the gaps, with strands trailing down to the floor."""
    C = (0.0, 0.0, 0.98)
    _dream_self(0.0, -0.04, 0.98, 1.15)
    # Open enough that the sleeper glows out through it.
    knot("loop", C, 11, (0.30, 0.42), (0.018, 0.028), 7, squash=(1.0, 0.95, 1.15))
    ring("cord_a", 0.40, 0.040, (0, 0, 0.92), "thread_lt", rot=(math.radians(12), math.radians(-10), 0), rough=0.35)
    ring("cord_b", 0.38, 0.034, (0, 0, 1.00), "thread_sheen", rot=(math.radians(82), 0, math.radians(35)),
         rough=0.35)
    # Strands down to the floor from its underside, and smoke round its foot.
    # Hanging in the air: only thin strands between it and the floor, and a
    # little smoke lying under it.
    for k, (x, y, fx, fy) in enumerate(((-0.12, -0.10, -0.32, -0.20), (0.14, -0.06, 0.34, -0.16),
                                        (0.02, 0.12, 0.10, 0.36), (-0.16, 0.10, -0.40, 0.22))):
        strand("hang_%d" % k, (x, y, 0.60), (fx, fy, 0.0), 0.022, 0.010, ("thread", "thread_lt")[k % 2], sag=-0.05)
    puffs("smoke", ((0.0, 0.06, 0.03, 0.30, 0.22, 0.03), (-0.36, 0.04, 1.30, 0.12, 0.10, 0.08),
                    (0.32, -0.04, 1.36, 0.10, 0.09, 0.07), (-0.40, 0.0, 0.80, 0.10, 0.09, 0.08),
                    (0.40, 0.02, 0.86, 0.09, 0.08, 0.07)))
    return centre(1.62)


def prop_mini_anchor():
    """A mini anchor: a knot of black thread no taller than a person, egg-
    shaped, standing on the cords it is rooted by, with threads trailing from
    it across the floor and dream smoke lying round its foot."""
    C = (0.0, 0.0, 0.66)
    sphere("core", 0.30, C, "thread_lt", rough=0.35).scale = (0.85, 0.85, 1.25)
    knot("loop", C, 18, (0.26, 0.36), (0.026, 0.040), 11, squash=(0.92, 0.92, 1.45))
    ring("cord_a", 0.33, 0.048, (0, 0, 0.62), "thread_lt", rot=(math.radians(14), math.radians(8), 0), rough=0.35)
    ring("cord_b", 0.32, 0.044, (0, 0, 0.70), "thread_sheen", rot=(math.radians(78), 0, math.radians(-30)),
         rough=0.35).scale = (1.0, 1.0, 1.35)
    for k in range(7):
        a = math.radians(k * 51 + 20)
        far = 0.42 + 0.14 * (k % 3)
        strand("root_%d" % k, (math.cos(a) * 0.16, math.sin(a) * 0.14, 0.30),
               (math.cos(a) * far, math.sin(a) * far * 0.8, 0.0), 0.045, 0.012, ("thread", "thread_lt")[k % 2],
               sag=-0.06)
    puffs("smoke", ((-0.26, -0.06, 0.06, 0.26, 0.18, 0.05), (0.24, 0.04, 0.06, 0.24, 0.18, 0.05),
                    (0.0, -0.20, 0.05, 0.22, 0.12, 0.04), (-0.24, 0.0, 0.98, 0.10, 0.09, 0.07)))
    return centre(1.45)


# =================================================================================
#  Scene 21 -- the Nightmare Anchor round the town well
# =================================================================================
# town_well is rendered at 136 pixels framing 3.3 units square; this is drawn
# at the very same scale, so laid over it at the same point it fits.
WELL_PPU = 136 / 3.3
SQUARE_ANCHOR_PX = 176


def prop_square_anchor():
    """The Nightmare Anchor in the Reverie's square: a colossal knot of black
    thread and dream smoke rooted in the ground round the town well, wound up
    its shaft and over its canopy, with strands reaching out of it every way
    -- one for each sleeper, across the roofs.

    Drawn to be laid over town_well, at the same bottom-centre point: the well
    is built here too, as a holdout, so the thread behind it is cut away and
    the well shows through wherever the knot leaves a gap. Nothing in front
    reaches lower than the well's own basin, so both pictures sit on the floor
    by the same row."""
    import blender_town_props as tp
    before = set(bpy.context.scene.objects)
    tp.prop_town_well()
    well = [ob for ob in bpy.context.scene.objects if ob not in before]
    for ob in well:
        ob.is_holdout = True

    # The knot round the shaft and the canopy.
    knot("low", (0, 0.05, 0.70), 26, (0.56, 0.86), (0.050, 0.085), 21, squash=(1.0, 0.85, 0.80), spread=0.14)
    knot("mid", (0, 0.05, 1.45), 22, (0.50, 0.84), (0.045, 0.080), 22, squash=(1.0, 0.85, 1.00), spread=0.14)
    knot("high", (0, 0.05, 2.25), 18, (0.50, 0.95), (0.045, 0.075), 23, squash=(1.05, 0.80, 0.80), spread=0.14)
    for k, (z, r_, tilt) in enumerate(((0.55, 0.78, 8), (1.10, 0.70, -12), (1.80, 0.74, 10), (2.40, 0.96, -6))):
        ring("cord_%d" % k, r_, 0.085, (0, 0.05, z), ("thread_lt", "thread")[k % 2],
             rot=(math.radians(tilt), math.radians(-tilt * 0.6), 0), rough=0.35).scale = (1.0, 0.82, 1.0)
    ring("cord_over", 0.95, 0.080, (0, 0.10, 1.70), "thread_sheen", rot=(math.radians(84), 0, math.radians(20)),
         rough=0.35).scale = (1.0, 1.0, 1.25)
    # Roots: thick cords down the well's steps and out over the basin wall,
    # behind its front edge and out to either side.
    for k, a in enumerate((200, 228, 256, 284, 312, 340, 20, 60, 100, 140, 165)):
        rad = math.radians(a)
        far = 1.55 + 0.25 * (k % 3)
        y_far = max(-1.30, math.sin(rad) * far * 0.85)
        strand("root_%d" % k, (math.cos(rad) * 0.55, math.sin(rad) * 0.45, 0.55),
               (math.cos(rad) * far, y_far, 0.04), 0.075, 0.020, ("thread", "thread_lt")[k % 2], sag=-0.20)
    # One cord laid over the basin's front and onto the ground in front of it,
    # so the lowest thing in both pictures is the same.
    strand("root_front", (0.20, -0.55, 0.50), (0.32, -1.40, 0.02), 0.070, 0.030, "thread", sag=-0.25)
    # Strands reaching out across the roofs, out of the frame.
    for k, (a, z0, z1, ln) in enumerate(((180, 2.2, 3.1, 2.4), (0, 2.0, 2.9, 2.4), (160, 1.4, 2.0, 2.4),
                                          (20, 1.5, 2.2, 2.4), (90, 2.6, 3.3, 1.6), (130, 2.4, 3.2, 2.0),
                                          (50, 2.5, 3.3, 2.0))):
        rad = math.radians(a)
        strand("reach_%d" % k, (math.cos(rad) * 0.5, math.sin(rad) * 0.4 + 0.1, z0),
               (math.cos(rad) * ln, math.sin(rad) * ln * 0.6 + 0.2, z1), 0.040, 0.016,
               ("thread_lt", "thread")[k % 2], sag=0.30)
    puffs("smoke", ((-1.0, 0.30, 0.30, 0.42, 0.30, 0.16), (1.0, 0.25, 0.32, 0.40, 0.30, 0.16),
                    (-0.6, 0.70, 0.25, 0.40, 0.28, 0.14), (0.6, 0.72, 0.26, 0.38, 0.28, 0.14),
                    (-1.25, -0.20, 0.18, 0.26, 0.20, 0.10), (1.25, -0.25, 0.18, 0.26, 0.20, 0.10),
                    (-0.9, 0.10, 1.40, 0.22, 0.18, 0.14), (0.95, 0.05, 1.55, 0.20, 0.16, 0.13),
                    (-0.5, 0.20, 2.75, 0.24, 0.18, 0.12), (0.55, 0.15, 2.85, 0.22, 0.17, 0.12)))
    return SQUARE_ANCHOR_PX / WELL_PPU


# =================================================================================
#  The barrier round the square
# =================================================================================
BARRIER_W = 2.0          # one segment's length across, its picture's full width
# Running up the screen a length of it is foreshortened by the camera: this
# long a run of it is exactly 64 pixels of picture at the same scale, so the
# upright pieces stack end to end with no seam.
BARRIER_V = BARRIER_W / math.sin(math.radians(bp.CAMERA_ELEVATION))


def _barrier(along_y=False, length=BARRIER_W):
    """A length of the barrier: a lattice of black glassy thread -- cords run
    the whole length, strands woven up through them -- with dream smoke along
    its foot. It is built three times end to end and the camera sees only the
    middle one, so whatever crosses one end of the picture comes back in at
    the other, and pieces laid end to end run on without a join."""
    def P(u, v, z):
        # u along the run, v across it.
        return (v, u, z) if along_y else (u, v, z)

    half = length / 2
    for g, off in enumerate((-length, 0.0, length)):
        for k, (z, r_, col) in enumerate(((0.18, 0.050, "thread"), (0.48, 0.045, "thread_lt"),
                                          (0.78, 0.042, "thread"), (1.04, 0.038, "thread_sheen"))):
            sag = 0.05 if k % 2 else -0.04
            n = 4
            for i in range(n):
                u0, u1 = off - half + length * i / n, off - half + length * (i + 1) / n
                # A gentle wave, the same at both ends of the length.
                z0 = z + sag * math.sin(math.pi * 2 * i / n)
                z1 = z + sag * math.sin(math.pi * 2 * (i + 1) / n)
                limb("cord_%d_%d_%d" % (g, k, i), P(u0, 0.0, z0), P(u1, 0.0, z1), r_, r_, col)
        # Strands woven up through the cords, leaning, and a few crossing.
        m = 6
        for i in range(m):
            u = off - half + length * (i + 0.5) / m
            lean = 0.16 if i % 2 else -0.16
            v = 0.07 if i % 3 == 0 else -0.05
            limb("strand_%d_%d" % (g, i), P(u - lean, -v, 0.0), P(u + lean, v, 1.20), 0.034, 0.020,
                 ("thread", "thread_lt")[i % 2])
            limb("tip_%d_%d" % (g, i), P(u + lean, v, 1.20), P(u + lean * 1.6, v * 1.4, 1.42 + 0.06 * (i % 3)),
                 0.020, 0.006, "thread_sheen" if i % 3 == 0 else "thread")
            uc = off - half + length * (i + 1.0) / m - length / (2 * m)
            limb("cross_%d_%d" % (g, i), P(uc - 0.30, 0.0, 0.30), P(uc + 0.30, 0.0, 0.92), 0.022, 0.022,
                 "thread_lt")
        # The wall itself: dark glass the thread is woven through, a little
        # short of the top so the strands' ends stand over it, with its sheen.
        if along_y:
            blk("glass_%d" % g, (0.05, length, 0.96), (0.0, off, 0.56), "thread", rough=0.15, bev=0)
            blk("glint_%d" % g, (0.055, length * 0.22, 0.05), (0.0, off - length * 0.18, 0.86), "thread_sheen",
                rough=0.15, bev=0)
        else:
            blk("glass_%d" % g, (length, 0.05, 0.96), (off, 0.0, 0.56), "thread", rough=0.15, bev=0)
            for i in range(2):
                blk("glint_%d_%d" % (g, i), (length * 0.18, 0.055, 0.05), (off - half + length * (0.2 + 0.5 * i),
                    -0.002, 0.86 - 0.30 * i), "thread_sheen", rough=0.15, bev=0)
        # Smoke along its foot.
        for i in range(4):
            u = off - half + length * (i + 0.5) / 4
            sm = sphere("foot_smoke_%d_%d" % (g, i), 1.0, P(u, -0.02, 0.08), ("dsmoke", "dsmoke_lt")[i % 2],
                        rough=0.9)
            sm.scale = (0.20, 0.30, 0.09) if along_y else (0.30, 0.20, 0.09)


def prop_dream_barrier():
    """The barrier round the Reverie's town square, a length of it running
    across the screen: laid end to end, 64 pixels apart, it rings the square,
    and it is what cracks and falls away like glass when the third Hold is
    released."""
    _barrier(False, BARRIER_W)
    return centre(BARRIER_W)


def prop_dream_barrier_v():
    """The same barrier running up the screen, cut narrow: every row of the
    picture is wall, and pieces stacked 64 pixels apart run on as one."""
    _barrier(True, BARRIER_V)
    return BARRIER_W


# =================================================================================
#  The Dawn Bells and the Dawn Chimes
# =================================================================================
bp.PALETTE.update({
    # Ivory rather than white, and lit only a little from within: lit more,
    # the bells were white blobs with no shape to them.
    "bell_pale":    (0.860, 0.830, 0.730), "bell_pale_dk": (0.640, 0.610, 0.540),
    "bell_glow":    (1.000, 0.940, 0.740),
    "weath_wood":   (0.500, 0.440, 0.380), "weath_wood_dk": (0.350, 0.300, 0.260),
    "weath_wood_lt": (0.620, 0.560, 0.480),
    "chime_bronze": (0.640, 0.500, 0.260), "chime_bronze_dk": (0.440, 0.330, 0.170),
    "chime_patina": (0.400, 0.540, 0.440), "chime_gold":   (1.000, 0.820, 0.400),
    "rope":         (0.700, 0.600, 0.420),
})

# A bell mouth-down: crown, shoulder, waist, and the lip flaring out.
BELL = ((0.001, 0.64), (0.12, 0.63), (0.21, 0.60), (0.26, 0.54), (0.28, 0.44), (0.285, 0.32), (0.30, 0.20),
        (0.34, 0.10), (0.40, 0.03), (0.45, -0.01), (0.44, -0.04), (0.001, -0.02))


def bell(name, x, y, z, s, colour="bell_pale", emit=0.12, lip="bell_glow", lip_emit=0.9):
    """A bell hung with its crown at (x, y, z), `s` times the size of BELL
    (which is 0.66 tall): the body, a lip a little lighter, the loop of the
    crown it hangs by, and the clapper showing under it."""
    prof = [(r_ * s, zz * s) for r_, zz in BELL]
    top = 0.64 * s
    lathe(name, prof, (x, y, z - top), colour, emit=emit, rough=0.45, metal=0.2)
    ring(name + "_lip", 0.44 * s, 0.035 * s, (x, y, z - top - 0.01 * s), lip, emit=lip_emit, rough=0.4)
    ring(name + "_band", 0.29 * s, 0.025 * s, (x, y, z - top + 0.32 * s), "bell_pale_dk", rough=0.5)
    ring(name + "_crown", 0.07 * s, 0.03 * s, (x, y, z + 0.03 * s), "bell_pale_dk", rot=(math.pi / 2, 0, 0))
    sphere(name + "_clapper", 0.08 * s, (x, y, z - top - 0.08 * s), "bell_pale_dk")


def _post(name, x, y, h, w=0.14, colour="weath_wood"):
    blk(name, (w, w, h), (x, y, h / 2), colour, bev=0.02)
    # A split down the weathered post.
    blk(name + "_split", (w * 0.12, w * 1.02, h * 0.55), (x + w * 0.18, y, h * 0.45), "weath_wood_dk", bev=0)


def prop_dawn_bells():
    """The Dawn Bells: a cluster of pale bells hung in an old bell frame of
    weathered wood -- two posts, braces, a beam under a little ridge of
    shingle -- the great one in the middle and the smaller ones either side,
    glowing faintly, the way they look when the dark peels off them."""
    for sx in (-1, 1):
        _post("post_%d" % sx, sx * 0.74, 0.0, 2.05)
        blk("foot_%d" % sx, (0.30, 0.40, 0.10), (sx * 0.74, 0.0, 0.05), "weath_wood_dk", bev=0.02)
        limb("brace_%d" % sx, (sx * 0.74, 0.0, 1.45), (sx * 0.46, 0.0, 1.88), 0.045, 0.045, "weath_wood_dk")
        limb("strut_%d" % sx, (sx * 0.74, 0.18, 0.05), (sx * 0.74, 0.05, 0.85), 0.040, 0.040, "weath_wood_dk")
    blk("beam", (1.70, 0.18, 0.16), (0, 0, 1.94), "weath_wood_lt", bev=0.02)
    blk("ridge", (1.78, 0.40, 0.08), (0, 0, 2.08), "shingle_dk", bev=0.02)
    blk("ridge_top", (1.72, 0.22, 0.08), (0, 0, 2.15), "shingle", bev=0.02)
    for k, (x, s) in enumerate(((-0.48, 0.46), (0.0, 0.74), (0.48, 0.46), (0.26, 0.30))):
        z = 1.86 if k < 3 else 1.40
        y = 0.0 if k < 3 else -0.18
        if k < 3:
            blk("yoke_%d" % k, (0.10 + 0.10 * s, 0.10, 0.10), (x, 0, 1.82), "weath_wood_dk", bev=0.01)
        else:
            limb("cord_3", (x, -0.02, 1.86), (x, y, z + 0.02), 0.015, 0.015, "rope")
        bell("bell_%d" % k, x, y, z - 0.04, s)
    blk("bar_lo", (1.46, 0.10, 0.10), (0, 0.0, 0.46), "weath_wood_dk", bev=0.01)
    return centre(2.30)


def prop_dawn_bell_great():
    """The last Dawn Bell of Havenbrook, in the Guild Hall: one great pale bell
    in an old frame -- heavy posts and braces, a beam over it -- glowing
    softly."""
    for sx in (-1, 1):
        _post("post_%d" % sx, sx * 0.86, 0.0, 2.55, w=0.18)
        blk("foot_%d" % sx, (0.42, 0.50, 0.12), (sx * 0.86, 0.0, 0.06), "weath_wood_dk", bev=0.02)
        limb("brace_%d" % sx, (sx * 0.86, 0.0, 1.85), (sx * 0.48, 0.0, 2.36), 0.055, 0.055, "weath_wood_dk")
        limb("leg_brace_%d" % sx, (sx * 0.86, -0.32, 0.06), (sx * 0.86, 0.0, 0.90), 0.050, 0.050, "weath_wood_dk")
        limb("leg_brace_b_%d" % sx, (sx * 0.86, 0.32, 0.06), (sx * 0.86, 0.0, 0.90), 0.050, 0.050, "weath_wood_dk")
    blk("beam", (2.06, 0.22, 0.20), (0, 0, 2.44), "weath_wood_lt", bev=0.02)
    blk("beam_cap", (2.16, 0.30, 0.06), (0, 0, 2.57), "weath_wood_dk", bev=0.02)
    blk("yoke", (0.42, 0.18, 0.14), (0, 0, 2.30), "weath_wood_dk", bev=0.02)
    for sx in (-1, 1):
        blk("strap_%d" % sx, (0.04, 0.20, 0.22), (sx * 0.12, 0, 2.18), "iron", bev=0)
    bell("bell", 0.0, 0.0, 2.18, 1.42, emit=0.18)
    return centre(2.75)


def prop_dawn_chime():
    """A Dawn Chime: a short string of small tarnished bronze chimes on a cord
    from an iron bracket, glowing faintly gold."""
    blk("plate", (0.12, 0.04, 0.22), (0, 0.20, 1.20), "iron", bev=0.01)
    limb("arm", (0, 0.20, 1.24), (0, -0.16, 1.24), 0.025, 0.025, "iron")
    limb("stay", (0, 0.20, 1.06), (0, -0.04, 1.24), 0.016, 0.016, "iron")
    ring("hook", 0.035, 0.012, (0, -0.16, 1.20), "iron", rot=(0, math.pi / 2, 0))
    limb("cord", (0, -0.16, 1.18), (0, -0.16, 0.28), 0.012, 0.012, "rope")
    # Three chimes, as big as the string will carry: four small ones were a
    # thread of beads at this size.
    for k, (z, s) in enumerate(((1.08, 0.30), (0.78, 0.26), (0.52, 0.22))):
        colour = "chime_bronze" if k % 2 == 0 else "chime_bronze_dk"
        bell("chime_%d" % k, 0.0, -0.16, z, s, colour=colour, emit=0.30, lip="chime_gold", lip_emit=1.3)
        sphere("patina_%d" % k, 0.035 * s / 0.2, (0.03, -0.16 - 0.08 * s / 0.2, z - 0.20 * s / 0.2),
               "chime_patina")
    sphere("tassel", 0.035, (0, -0.16, 0.26), "chime_gold", emit=0.9)
    return centre(1.20)


# =================================================================================
#  Scenes 31-35 -- Halda's forge, and the forge she dreams
# =================================================================================
bp.PALETTE.update({
    "chain_black": (0.120, 0.110, 0.130), "chain_black_lt": (0.260, 0.240, 0.280),
    "chair_oak":   (0.420, 0.270, 0.160), "chair_oak_dk": (0.300, 0.190, 0.110),
    "chair_oak_lt": (0.540, 0.360, 0.220),
    "anvil_iron":  (0.250, 0.250, 0.280), "anvil_iron_dk": (0.150, 0.150, 0.170),
    "anvil_face":  (0.620, 0.620, 0.660), "anvil_hot":   (1.000, 0.450, 0.120),
})


def chain(name, a, b, link=0.10, r=0.030, colours=("chain_black", "chain_black_lt"), slack=0.0):
    """A chain of links from a to b, each turned a quarter to the last; `slack`
    lets it hang in a curve between its ends."""
    a, b = Vector(a), Vector(b)
    n = max(2, int((b - a).length / link))
    pts = []
    for i in range(n + 1):
        f = i / n
        p = a.lerp(b, f)
        p.z -= slack * 4.0 * f * (1.0 - f)
        pts.append(p)
    for i in range(n):
        p0, p1 = pts[i], pts[i + 1]
        d = (p1 - p0)
        mid = (p0 + p1) / 2
        q = Vector((0, 0, 1)).rotation_difference(d.normalized())
        ob = ring("%s_%d" % (name, i), link * 0.42, r * 0.55, tuple(mid), colours[i % 2], rough=0.4, metal=0.3)
        ob.rotation_mode = "QUATERNION"
        # A link's loop stands along the chain, every other one a quarter round.
        ob.rotation_quaternion = q @ Euler((math.pi / 2, 0, (math.pi / 2) * (i % 2)), "XYZ").to_quaternion()
        ob.scale = (1.0, 1.55, 1.0)


def _heavy_chair():
    """A heavy chair: thick square legs, solid arms on posts, a tall back of
    three broad slats under a top rail -- a smith's chair."""
    W, D = 0.66, 0.60
    blk("seat", (W, D, 0.10), (0, 0, 0.50), "chair_oak_lt", bev=0.02)
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.11, 0.11, 0.50), (sx * (W / 2 - 0.06), sy * (D / 2 - 0.06), 0.25),
                "chair_oak", bev=0.015)
        blk("arm_post_%d" % sx, (0.09, 0.09, 0.30), (sx * (W / 2 - 0.05), -D / 2 + 0.07, 0.70), "chair_oak", bev=0.015)
        blk("arm_%d" % sx, (0.12, D + 0.04, 0.08), (sx * (W / 2 - 0.04), 0.0, 0.88), "chair_oak_lt", bev=0.02)
        blk("upright_%d" % sx, (0.11, 0.11, 0.86), (sx * (W / 2 - 0.06), D / 2 - 0.06, 0.98), "chair_oak", bev=0.015)
    for k in range(3):
        blk("slat_%d" % k, (0.14, 0.05, 0.60), (-0.18 + k * 0.18, D / 2 - 0.05, 1.00), "chair_oak_dk", bev=0.01)
    blk("top_rail", (W + 0.06, 0.12, 0.12), (0, D / 2 - 0.06, 1.43), "chair_oak_lt", bev=0.02)
    blk("stretcher", (W - 0.10, 0.06, 0.06), (0, -D / 2 + 0.06, 0.16), "chair_oak_dk", bev=0.01)
    return W, D


CHAIR_SPAN = 1.40 * 80 / 48


def prop_black_chains_chair():
    """Halda's chair as the forge has it: a heavy wooden chair bound round with
    black chains -- round the back, over the arms, across the seat -- and
    three of them running straight up out of the picture to the rafters."""
    W, D = _heavy_chair()
    for k, z in enumerate((0.70, 1.02, 1.30)):
        # Round the back and the arms, sagging a little across the front.
        chain("wrap_%d_a" % k, (-W / 2 - 0.02, D / 2 - 0.02, z), (W / 2 + 0.02, D / 2 - 0.02, z + 0.04))
        chain("wrap_%d_b" % k, (-W / 2 - 0.02, D / 2 - 0.02, z + 0.02), (-W / 2 - 0.02, -D / 2 + 0.06, z - 0.12))
        chain("wrap_%d_c" % k, (W / 2 + 0.02, D / 2 - 0.02, z + 0.04), (W / 2 + 0.02, -D / 2 + 0.06, z - 0.10))
    chain("across", (-W / 2, -D / 2 + 0.10, 0.62), (W / 2, -D / 2 + 0.10, 0.60), slack=0.06)
    # Straight up into the dark, from the back's two corners and the front of
    # one arm.
    for k, (x, y) in enumerate(((-W / 2 + 0.06, D / 2 - 0.06), (W / 2 - 0.06, D / 2 - 0.06),
                                (W / 2 - 0.04, -D / 2 + 0.1))):
        chain("up_%d" % k, (x, y, 1.40 if k < 2 else 0.90), (x, y, 4.2), link=0.11)
    return CHAIR_SPAN


def prop_black_chains_chair_empty():
    """The same chair once the knights are down: the chains gone slack and
    fallen, heaped on the seat and round the chair's feet -- inside its own
    footprint, so the two pictures sit on the floor at the same row."""
    W, D = _heavy_chair()
    rnd = random.Random(5)
    for k, (x, y, z, n) in enumerate(((-0.22, 0.02, 0.58, 6), (0.18, -0.04, 0.58, 5), (-0.40, 0.10, 0.04, 7),
                                      (0.40, 0.06, 0.04, 7), (0.0, 0.18, 0.04, 6))):
        for i in range(n):
            a = rnd.uniform(0, math.tau)
            rr = rnd.uniform(0.02, 0.12)
            ob = ring("pile_%d_%d" % (k, i), 0.042, 0.017, (x + math.cos(a) * rr, y + math.sin(a) * rr * 0.7,
                                                             z + 0.012 * i), ("chain_black", "chain_black_lt")[i % 2],
                      rot=(rnd.uniform(-0.8, 0.8), rnd.uniform(-0.8, 0.8), rnd.uniform(0, 3)), rough=0.4, metal=0.3)
            ob.scale = (1.0, 1.55, 1.0)
    # One length still hanging off the arm to the floor.
    chain("hang", (W / 2 + 0.02, 0.0, 0.88), (W / 2 + 0.10, 0.12, 0.04), slack=-0.05)
    return CHAIR_SPAN


def anvil(prefix, L, H, x=0.0, y=0.0, face="anvil_face"):
    """An anvil along X: a spread foot, a waist, the body and its worn face on
    top, the horn out to the left and the heel to the right with its hardy
    hole. L is its length horn to heel, H its height."""
    W = H * 0.62
    blk(prefix + "_foot", (L * 0.52, W * 1.15, H * 0.16), (x + L * 0.04, y, H * 0.08), "anvil_iron_dk", bev=0.03 * H)
    blk(prefix + "_foot_hi", (L * 0.44, W * 0.95, H * 0.10), (x + L * 0.04, y, H * 0.20), "anvil_iron", bev=0.02 * H)
    blk(prefix + "_waist", (L * 0.30, W * 0.70, H * 0.34), (x + L * 0.04, y, H * 0.42), "anvil_iron_dk", bev=0.02 * H)
    blk(prefix + "_body", (L * 0.62, W, H * 0.24), (x + L * 0.08, y, H * 0.70), "anvil_iron", bev=0.02 * H)
    blk(prefix + "_face", (L * 0.60, W * 0.94, H * 0.05), (x + L * 0.08, y, H * 0.84), face, rough=0.35,
        metal=0.4, bev=0.01 * H)
    limb(prefix + "_horn", (x - L * 0.22, y, H * 0.74), (x - L * 0.50, y, H * 0.78), H * 0.15, H * 0.02, "anvil_iron",
         verts=16)
    blk(prefix + "_heel", (L * 0.12, W * 0.80, H * 0.20), (x + L * 0.44, y, H * 0.74), "anvil_iron", bev=0.02 * H)
    blk(prefix + "_hardy", (H * 0.07, H * 0.07, H * 0.02), (x + L * 0.30, y, H * 0.865), "void", bev=0)


def prop_giant_anvil():
    """An anvil the size of a cottage, from Halda's dream forge: dark iron, the
    working face worn bright, a scorch of heat still in one end of it."""
    anvil("anvil", 2.70, 1.30, x=0.10)
    for k, (x, y) in enumerate(((0.55, -0.18), (0.80, 0.10), (-0.10, 0.12))):
        sphere("scar_%d" % k, 0.07, (x, y, 1.105), "anvil_iron_dk").scale = (1.4, 1.0, 0.25)
    blk("heat", (0.30, 0.50, 0.04), (0.95, 0.0, 1.10), "anvil_hot", emit=1.4, bev=0.01)
    return centre(2.90)


def prop_hanging_hammer():
    """A smith's hammer as big as a door, hung head-down from the rafters on a
    chain like a silent bell: the chain out of the top of the picture, the
    haft hanging from it, and the head at the bottom."""
    blk("head", (0.86, 0.50, 0.52), (0, 0, 0.36), "anvil_iron", bev=0.04)
    blk("face_a", (0.07, 0.46, 0.48), (-0.46, 0, 0.36), "anvil_face", rough=0.35, metal=0.4, bev=0.015)
    blk("face_b", (0.07, 0.46, 0.48), (0.46, 0, 0.36), "anvil_face", rough=0.35, metal=0.4, bev=0.015)
    blk("collar", (0.28, 0.54, 0.16), (0, 0, 0.68), "anvil_iron_dk", bev=0.02)
    cyl("haft", 0.10, 1.50, (0, 0, 1.48), "chair_oak", verts=12)
    for z in (1.10, 2.05):
        cyl("wrap_%d" % int(z * 10), 0.112, 0.12, (0, 0, z), "leather", verts=12)
    ring("eye", 0.10, 0.03, (0, 0, 2.30), "chain_black_lt", rot=(math.pi / 2, 0, 0))
    chain("chain", (0, 0, 2.38), (0, 0, 4.4), link=0.14, r=0.040)
    # Turned a little on its chain, so the head shows its length and both its
    # faces: square on, it was a block on a stick.
    turn_all(34)
    return 3.0


def prop_great_anvil_bound():
    """The great anvil at the middle of Halda's dream platform, wrapped in a
    knot of black thread: the Anchor, until it is broken."""
    anvil("anvil", 1.70, 0.86)
    # Round its middle only, so the horn and the heel stand out of it and it
    # is still an anvil.
    knot("knot", (0.06, 0.0, 0.56), 16, (0.30, 0.44), (0.028, 0.045), 41, squash=(1.05, 0.95, 0.85), spread=0.08)
    ring("cord_a", 0.46, 0.055, (0.06, 0.0, 0.58), "thread_lt", rot=(math.radians(10), math.radians(-12), 0),
         rough=0.35).scale = (1.15, 0.85, 1.0)
    ring("cord_b", 0.42, 0.050, (0.1, 0.0, 0.62), "thread_sheen", rot=(math.radians(80), 0, math.radians(12)),
         rough=0.35).scale = (1.0, 1.0, 1.1)
    for k in range(6):
        a = math.radians(k * 60 + 20)
        strand("root_%d" % k, (math.cos(a) * 0.45, math.sin(a) * 0.30, 0.40),
               (math.cos(a) * 1.02, math.sin(a) * 0.62, 0.0), 0.040, 0.012, ("thread", "thread_lt")[k % 2], sag=-0.05)
    return centre(2.15)


# =================================================================================
#  Scenes 41-43 -- the tannery, and the winter yard the Tanner dreams
# =================================================================================
bp.PALETTE.update({
    "hide_grey":   (0.600, 0.580, 0.560), "hide_grey_dk": (0.420, 0.400, 0.400),
    "hide_dark":   (0.440, 0.330, 0.240), "mast_wood":    (0.360, 0.300, 0.250),
    "mast_wood_dk": (0.250, 0.200, 0.170), "vat_liquor":  (0.200, 0.140, 0.090),
    "steam_cold":  (0.800, 0.860, 0.940), "steam_cold_dk": (0.620, 0.700, 0.800),
    "frost":       (0.860, 0.920, 0.980), "frost_dk":     (0.640, 0.740, 0.860),
    "frost_glint": (1.000, 1.000, 1.000),
    "wolf_fur":    (0.560, 0.550, 0.540), "wolf_fur_dk":  (0.380, 0.370, 0.380),
    "wolf_belly":  (0.780, 0.760, 0.720), "wolf_back":   (0.260, 0.250, 0.270),
})


def _hide(name, x, y, z, w, h, colour, sway=0.0, turn=0.0):
    """A hide hung by its top edge like a banner: a long ragged sheet of skin,
    a darker stripe down the spine, the legs' flaps pulled out at its four
    corners, and the neck end hanging lowest. A lighter panel in the middle of
    it made every hide a framed picture."""
    m = Euler((0, math.radians(sway), math.radians(turn)), "XYZ").to_matrix()
    rot = (0, math.radians(sway), math.radians(turn))

    def at(dx, dz):
        o = m @ Vector((dx, 0.0, dz))
        return (x + o.x, y + o.y, z + o.z)
    blk(name, (w, 0.03, h), at(0, -h / 2), colour, rot=rot, bev=0.012)
    blk(name + "_spine", (w * 0.16, 0.034, h * 0.86), at(0, -h * 0.47), "hide_dark" if colour != "hide_dark"
        else "mast_wood_dk", rot=rot, bev=0.006)
    blk(name + "_neck", (w * 0.36, 0.03, h * 0.16), at(0, -h * 1.04), colour, rot=rot, bev=0.012)
    for sx in (-1, 1):
        for k, fz in enumerate((-0.12, -0.80)):
            tilt = sx * (24 if k else -18)
            blk("%s_leg_%d_%d" % (name, sx, k), (w * 0.20, 0.03, h * 0.30), at(sx * w * 0.56, h * fz), colour,
                rot=(0, math.radians(sway + tilt), math.radians(turn)), bev=0.01)


def prop_drying_frame_tall():
    """A drying frame as tall as a ship's mast, from the Tanner's dream: a
    mast stepped in a heavy foot and stayed with ropes, three yards across it,
    and hides hung from every yard like banners, stirring though there is no
    wind."""
    cyl("mast", 0.075, 3.70, (0, 0, 1.85), "mast_wood", verts=12)
    blk("step", (0.40, 0.40, 0.16), (0, 0, 0.08), "mast_wood_dk", bev=0.02)
    for k, (z, half, col) in enumerate(((1.50, 0.78, "hide_grey"), (2.50, 0.66, "hide_tan"), (3.38, 0.52, "hide_dark"))):
        blk("yard_%d" % k, (half * 2, 0.07, 0.07), (0, -0.02, z), "mast_wood_dk", bev=0.01)
        # Longer than they are wide, and swung out of true: banners.
        _hide("hide_%d_a" % k, -half * 0.50, -0.05, z - 0.04, half * 0.62, 0.78 - 0.10 * k, col, sway=5 + 3 * k)
        _hide("hide_%d_b" % k, half * 0.52, -0.05, z - 0.04, half * 0.58, 0.70 - 0.08 * k,
              ("hide_tan", "hide_dark", "hide_grey")[k], sway=-6 - 2 * k)
        for sx in (-1, 1):
            limb("lift_%d_%d" % (k, sx), (sx * half, -0.02, z), (0, 0.0, 3.68), 0.010, 0.010, "rope")
    for sx in (-1, 1):
        limb("stay_%d" % sx, (0, 0.04, 3.30), (sx * 0.80, 0.30, 0.02), 0.012, 0.012, "rope")
        blk("peg_%d" % sx, (0.06, 0.06, 0.10), (sx * 0.80, 0.30, 0.05), "mast_wood_dk", bev=0.01)
    sphere("truck", 0.08, (0, 0, 3.74), "mast_wood_dk")
    return centre(4.0)


def prop_tannery_post_bound():
    """The post in the dream yard the Tanner stands with her back to: a stout
    wooden post with a thick black thread knotted round it low down, about
    where her belt is. The thread from the knot to her belt is not in the
    picture: the game draws it (a "tether", genmaps BuildDreamTannery), taut
    while the wolves circle and slack once the fourth one falls, and it has
    to go wherever she stands."""
    cyl("post", 0.11, 1.55, (0, 0, 0.775), "mast_wood", verts=12)
    sphere("post_top", 0.11, (0, 0, 1.55), "mast_wood_dk").scale = (1.0, 1.0, 0.4)
    blk("split", (0.02, 0.23, 0.70), (0.04, 0, 1.00), "mast_wood_dk", bev=0)
    knot("knot", (0, 0, 0.36), 16, (0.16, 0.24), (0.034, 0.050), 51, squash=(1.0, 1.0, 0.65), spread=0.03)
    ring("cord", 0.18, 0.065, (0, 0, 0.32), "thread_lt", rot=(math.radians(8), math.radians(-6), 0), rough=0.35)
    ring("cord_b", 0.17, 0.055, (0, 0, 0.42), "thread_sheen", rot=(math.radians(-10), math.radians(8), 0),
         rough=0.35)
    return centre(1.90)


def prop_steaming_vat():
    """A tanning vat in the winter yard: a broad tub of staves and iron hoops,
    the dark liquor in it, and steam rising off it cold and pale."""
    slices = 8
    for i in range(slices):
        t = (i + 0.5) / slices
        cyl("stave_%d" % i, 0.42 + 0.03 * math.sin(t * math.pi), 0.62 / slices + 0.002, (0, 0, 0.03 + t * 0.62),
            "oak", verts=24)
    for z in (0.14, 0.52):
        cyl("hoop_%d" % int(z * 100), 0.46, 0.05, (0, 0, z), "iron", metal=0.6, rough=0.5, verts=24)
    cyl("rim", 0.43, 0.04, (0, 0, 0.66), "oak_light", verts=24)
    cyl("liquor", 0.39, 0.03, (0, 0, 0.62), "vat_liquor", rough=0.2, verts=24)
    limb("paddle", (0.16, 0.08, 0.60), (0.36, 0.24, 1.05), 0.025, 0.025, "oak_light")
    # Wisps, not a head of foam: a few thin curls off the surface.
    puffs("steam", ((-0.12, 0.02, 0.76, 0.10, 0.08, 0.07), (0.10, -0.04, 0.80, 0.09, 0.07, 0.06),
                    (-0.06, 0.04, 0.92, 0.06, 0.05, 0.08), (0.08, 0.0, 0.98, 0.05, 0.05, 0.07),
                    (-0.02, 0.02, 1.08, 0.04, 0.04, 0.06)),
          colours=("steam_cold", "steam_cold_dk"))
    return centre(1.36)


def prop_wolf_pelt_rack():
    """A rack of grey wolf pelts in the tannery yard: a frame of poles, and the
    pelts slung over its bar with the heads hanging down in front -- the empty
    eyeholes looking out."""
    for sx in (-1, 1):
        cyl("pole_%d" % sx, 0.045, 1.20, (sx * 0.62, 0, 0.60), "mast_wood", verts=10)
        limb("leg_%d" % sx, (sx * 0.62, 0, 0.90), (sx * 0.62, 0.30, 0.0), 0.035, 0.035, "mast_wood_dk")
    cyl("bar", 0.045, 1.40, (0, 0, 1.12), "mast_wood_dk", rot=(0, math.pi / 2, 0), verts=10)
    for k, x in enumerate((-0.38, 0.0, 0.38)):
        colour = ("wolf_fur_dk", "wolf_fur", "wolf_fur_dk")[k]
        # Over the bar: the hindquarters behind it, the forequarters hanging
        # in front, the forelegs splayed out and the head at the bottom with
        # its ears and its empty eyes.
        blk("pelt_back_%d" % k, (0.26, 0.04, 0.36), (x, 0.07, 0.94), colour, rot=(math.radians(-8), 0, 0), bev=0.02)
        blk("pelt_front_%d" % k, (0.24, 0.04, 0.44), (x, -0.06, 0.88), colour, rot=(math.radians(8), 0, 0), bev=0.02)
        blk("stripe_%d" % k, (0.08, 0.046, 0.40), (x, -0.075, 0.88), "wolf_fur_dk" if colour == "wolf_fur" else "wolf_back",
            rot=(math.radians(8), 0, 0), bev=0.01)
        for sx in (-1, 1):
            blk("hind_%d_%d" % (k, sx), (0.06, 0.04, 0.22), (x + sx * 0.15, 0.09, 1.02), colour,
                rot=(math.radians(-8), sx * 0.9, 0), bev=0.01)
            blk("fore_%d_%d" % (k, sx), (0.06, 0.04, 0.24), (x + sx * 0.16, -0.08, 0.76), colour,
                rot=(math.radians(8), -sx * 0.55, 0), bev=0.01)
        head = sphere("head_%d" % k, 0.10, (x, -0.11, 0.58), colour)
        head.scale = (1.0, 0.55, 1.0)
        sphere("muzzle_%d" % k, 0.055, (x, -0.14, 0.48), "wolf_belly").scale = (0.75, 0.7, 1.25)
        sphere("nose_%d" % k, 0.022, (x, -0.17, 0.43), "void")
        for sx in (-1, 1):
            sphere("eyehole_%d_%d" % (k, sx), 0.024, (x + sx * 0.042, -0.165, 0.60), "void")
            cone("ear_%d_%d" % (k, sx), 0.045, 0.09, (x + sx * 0.075, -0.09, 0.68), colour, verts=6)
    return centre(1.50)


def prop_frost_patch():
    """Frost on the yard's stones in broad daylight: a ragged patch of white
    rime grown in feathers out from its middle, glittering."""
    rnd = random.Random(61)
    # Ragged drifts of rime run together, not a plate: on a square of it the
    # patch read as a tile of ice.
    for k in range(9):
        a = rnd.uniform(0, math.tau)
        r_ = rnd.uniform(0.15, 0.45)
        sphere("drift_%d" % k, 1.0, (math.cos(a) * r_, math.sin(a) * r_, 0.008), "frost").scale = (
            rnd.uniform(0.18, 0.34), rnd.uniform(0.14, 0.28), 0.010)
    sphere("drift_mid", 1.0, (0.0, 0.0, 0.009), "frost").scale = (0.36, 0.30, 0.010)
    # Feathers of it, growing out from the middle in branching strokes.
    for k in range(14):
        a = k / 14 * math.tau + rnd.uniform(-0.1, 0.1)
        ln = rnd.uniform(0.45, 0.78)
        base = Vector((math.cos(a) * 0.10, math.sin(a) * 0.10, 0.02))
        tip = Vector((math.cos(a) * ln, math.sin(a) * ln, 0.02))
        limb("vein_%d" % k, base, tip, 0.020, 0.008, "frost")
        for j in range(3):
            f = 0.35 + 0.22 * j
            p = base.lerp(tip, f)
            for side in (-1, 1):
                b = a + side * 0.7
                q = p + Vector((math.cos(b), math.sin(b), 0.0)) * (0.10 * (1.0 - f) + 0.04)
                limb("barb_%d_%d_%d" % (k, j, side), p, q, 0.012, 0.005, "frost_dk" if j % 2 else "frost")
    for k in range(9):
        a = rnd.uniform(0, math.tau)
        r_ = rnd.uniform(0.1, 0.6)
        sphere("glint_%d" % k, 0.022, (math.cos(a) * r_, math.sin(a) * r_, 0.03), "frost_glint", emit=2.0)
    return centre(1.55)


# =================================================================================
#  Scenes 36-40 -- the inn, its cellar, and the cellar Bess dreams
# =================================================================================
bp.PALETTE.update({
    "web":         (0.150, 0.140, 0.170), "web_lt":      (0.360, 0.340, 0.400),
    "apron_cloth": (0.860, 0.820, 0.700), "apron_dk":    (0.700, 0.660, 0.560),
    "loaf":        (0.760, 0.560, 0.300), "loaf_dk":     (0.600, 0.420, 0.220),
    "loaf_crumb":  (0.880, 0.800, 0.620), "linen":       (0.880, 0.850, 0.760),
    "stitch_red":  (0.720, 0.180, 0.160), "stitch_blue": (0.240, 0.360, 0.620),
    "stitch_gold": (0.880, 0.680, 0.240), "stitch_green": (0.300, 0.520, 0.300),
    "stitch_brown": (0.440, 0.300, 0.180), "candle_wax": (0.930, 0.900, 0.800),
})


def prop_cask_stack_tall():
    """Bess's casks as she dreams them: stacked impossibly high, ends out, every
    tier set a little off the one under it so the whole tower leans, one more
    about to go."""
    rows = ((2, 0.0), (2, 0.05), (1, 0.11), (2, 0.16), (1, 0.24), (1, 0.33), (1, 0.43))
    z = 0.26
    for k, (n, lean) in enumerate(rows):
        for i in range(n):
            x = lean + (i - (n - 1) / 2) * 0.50
            bp.lying_barrel("cask_%d_%d" % (k, i), x, 0.0, z, radius=0.25, length=0.56, tap=(k == 0))
        z += 0.48 + (0.02 if k % 2 else 0.0)
    # Chocks under the bottom tier, and the top cask slewed round.
    blk("chock_l", (0.14, 0.50, 0.06), (-0.26, 0, 0.03), "oak_light", bev=0.01)
    blk("chock_r", (0.14, 0.50, 0.06), (0.26, 0, 0.03), "oak_light", bev=0.01)
    return centre(4.0)


def prop_web_patch():
    """Black webbing to lay over whatever it has taken -- casks, shelves, a
    stair: spokes out from a hub up in one corner, rings of thread strung
    between them sagging, a torn hole and loose strands."""
    hub = Vector((-0.10, 0.0, 0.70))
    ends = [(-0.62, 0.86), (-0.60, 0.30), (-0.42, -0.02), (0.02, -0.04), (0.50, 0.06), (0.62, 0.52),
            (0.56, 0.92), (0.10, 1.00)]
    spokes = []
    for k, (x, z) in enumerate(ends):
        e = Vector((x, -0.02, z))
        limb("spoke_%d" % k, hub, e, 0.012, 0.010, ("web", "web_lt")[k % 2])
        spokes.append(e)
    for ring_k, f in enumerate((0.22, 0.42, 0.62, 0.82)):
        for k in range(len(spokes)):
            if ring_k == 2 and k in (3, 4):
                continue                       # torn through
            a = hub.lerp(spokes[k], f)
            b = hub.lerp(spokes[(k + 1) % len(spokes)], f)
            if (spokes[k] - spokes[(k + 1) % len(spokes)]).length > 1.0:
                continue
            strand("ring_%d_%d" % (ring_k, k), a, b, 0.010, 0.010, ("web", "web_lt")[(k + ring_k) % 2],
                   sag=0.03 * f, segs=2)
    for k, (x, z, ln) in enumerate(((0.32, 0.30, 0.30), (-0.30, 0.20, 0.26))):
        limb("loose_%d" % k, (x, -0.02, z), (x + 0.06, -0.04, z - ln), 0.010, 0.006, "web")
    return 1.20


def prop_web_strand():
    """A long strand of black web hanging from a rafter, with a few tangles in
    it and something small caught at the end."""
    limb("strand_a", (0, 0, 1.60), (0.02, 0, 0.90), 0.014, 0.012, "web_lt")
    limb("strand_b", (0.02, 0, 0.90), (-0.02, 0, 0.22), 0.012, 0.010, "web")
    for k, z in enumerate((1.20, 0.72)):
        sphere("tangle_%d" % k, 0.045, (0.01, 0, z), "web").scale = (1.0, 0.6, 1.5)
    sphere("caught", 0.05, (-0.02, 0, 0.16), "web_lt").scale = (0.9, 0.7, 1.3)
    limb("drip", (-0.02, 0, 0.11), (-0.02, 0, 0.03), 0.008, 0.004, "web")
    return centre(1.65)


def prop_apron_hook():
    """Bess's apron left on its hook: a peg in a little board on the wall, the
    apron hung from its neck strap, its ties trailing."""
    blk("board", (0.20, 0.04, 0.12), (0, 0.12, 1.05), "oak", bev=0.01)
    limb("peg", (0, 0.10, 1.05), (0, -0.04, 1.08), 0.020, 0.018, "oak_light")
    limb("strap_l", (0, -0.03, 1.07), (-0.10, -0.02, 0.88), 0.012, 0.012, "apron_dk")
    limb("strap_r", (0, -0.03, 1.07), (0.10, -0.02, 0.88), 0.012, 0.012, "apron_dk")
    blk("bib", (0.22, 0.03, 0.20), (0, -0.03, 0.80), "apron_cloth", bev=0.01)
    blk("skirt", (0.30, 0.03, 0.36), (0, -0.04, 0.52), "apron_cloth", rot=(math.radians(4), 0, 0), bev=0.01)
    blk("pocket", (0.16, 0.035, 0.08), (0, -0.06, 0.52), "apron_dk", bev=0.005)
    blk("hem", (0.30, 0.035, 0.03), (0, -0.05, 0.35), "apron_dk", bev=0)
    for sx in (-1, 1):
        limb("tie_%d" % sx, (sx * 0.14, -0.04, 0.70), (sx * 0.17, -0.06, 0.40), 0.010, 0.008, "apron_dk")
    return centre(0.80)


def prop_mug_half():
    """A half-full mug of ale left on the bar: a wooden mug with iron bands and
    a handle, the ale standing halfway down it."""
    lathe("mug", ((0.001, 0.30), (0.11, 0.30), (0.12, 0.28), (0.12, 0.02), (0.11, 0.0), (0.001, 0.0)),
          (0, 0, 0), "oak_light", rough=0.6)
    lathe("hollow", ((0.001, 0.31), (0.095, 0.31), (0.095, 0.30), (0.001, 0.30)), (0, 0, 0), "void")
    cyl("ale", 0.096, 0.02, (0, 0, 0.17), "ale", rough=0.3, verts=20)
    for z in (0.07, 0.24):
        cyl("band_%d" % int(z * 100), 0.124, 0.025, (0, 0, z), "iron", metal=0.6, rough=0.5, verts=20)
    ring("handle", 0.07, 0.022, (0.14, 0, 0.16), "oak", rot=(math.pi / 2, 0, 0))
    return 0.42


def prop_loaf_rack():
    """A cooling rack with a stale loaf left on it: an iron grid on little
    feet, and a round loaf with its crust split."""
    for k in range(6):
        limb("rod_%d" % k, (-0.36 + k * 0.144, -0.22, 0.12), (-0.36 + k * 0.144, 0.22, 0.12), 0.012, 0.012, "iron")
    for y in (-0.22, 0.0, 0.22):
        limb("cross_%d" % int(y * 100), (-0.38, y, 0.13), (0.38, y, 0.13), 0.012, 0.012, "iron")
    for sx in (-1, 1):
        for sy in (-1, 1):
            limb("foot_%d_%d" % (sx, sy), (sx * 0.34, sy * 0.20, 0.12), (sx * 0.36, sy * 0.22, 0.0), 0.012, 0.012,
                 "iron")
    loaf = sphere("loaf", 0.22, (0.02, 0, 0.24), "loaf")
    loaf.scale = (1.25, 0.85, 0.62)
    blk("split", (0.30, 0.04, 0.03), (0.02, -0.02, 0.37), "loaf_crumb", rot=(0, 0, 0.2), bev=0.01)
    blk("score_a", (0.04, 0.16, 0.02), (-0.12, 0, 0.36), "loaf_dk", rot=(0, 0, 0.4), bev=0)
    blk("score_b", (0.04, 0.16, 0.02), (0.16, 0, 0.36), "loaf_dk", rot=(0, 0, 0.4), bev=0)
    return 0.92


def prop_candlestick():
    """The candlestick on the bedside table upstairs: a little round table on
    one leg, a brass candlestick with a dish and a ring to lift it by, and the
    candle in it burnt half down."""
    cyl("table_top", 0.20, 0.04, (0, 0, 0.56), "oak_light", verts=20)
    cyl("table_leg", 0.035, 0.54, (0, 0, 0.27), "oak", verts=10)
    for k in range(3):
        a = k / 3 * math.tau + 0.4
        limb("table_foot_%d" % k, (0, 0, 0.10), (math.cos(a) * 0.16, math.sin(a) * 0.16, 0.0), 0.022, 0.018, "oak")
    cyl("dish", 0.10, 0.025, (0, 0, 0.59), "brass", metal=0.7, rough=0.35, verts=20)
    cyl("stem", 0.025, 0.12, (0, 0, 0.66), "brass", metal=0.7, rough=0.35, verts=10)
    cyl("cup", 0.04, 0.04, (0, 0, 0.73), "brass", metal=0.7, rough=0.35, verts=12)
    ring("lift", 0.035, 0.010, (0.10, 0, 0.60), "brass", rot=(math.pi / 2, 0, 0), metal=0.7)
    cyl("candle", 0.03, 0.16, (0, 0, 0.83), "candle_wax", verts=12)
    sphere("drip", 0.018, (0.026, -0.006, 0.84), "candle_wax")
    limb("wick", (0, 0, 0.91), (0.004, 0, 0.94), 0.005, 0.004, "coal")
    return centre(0.95)


def prop_sampler():
    """The cross-stitched sampler over the common room's hearth, in a plain
    frame: a red border, and in a row across it the three things it says to
    do and in that order -- a candle's flame, a loaf, a mug -- over a line of
    stitched words and a sun and a moon in the corners."""
    blk("frame", (0.92, 0.05, 0.68), (0, 0.06, 0.60), "oak", bev=0.01)
    blk("linen", (0.80, 0.05, 0.56), (0, 0.04, 0.60), "linen", rough=0.9, bev=0)
    y = 0.012

    def st(name, x, z, w, h, colour):
        blk(name, (w, 0.02, h), (x, y, z), colour, rough=0.9, bev=0)
    # The border.
    st("border_t", 0, 0.84, 0.74, 0.03, "stitch_red")
    st("border_b", 0, 0.36, 0.74, 0.03, "stitch_red")
    st("border_l", -0.355, 0.60, 0.03, 0.48, "stitch_red")
    st("border_r", 0.355, 0.60, 0.03, 0.48, "stitch_red")
    # The candle: a stick with its flame.
    st("candle", -0.22, 0.62, 0.05, 0.14, "linen")
    st("candle_body", -0.22, 0.61, 0.05, 0.12, "stitch_blue")
    st("flame", -0.22, 0.72, 0.05, 0.07, "stitch_gold")
    st("candle_dish", -0.22, 0.54, 0.10, 0.025, "stitch_brown")
    # The loaf.
    st("loaf", 0.0, 0.62, 0.15, 0.08, "stitch_brown")
    st("loaf_top", 0.0, 0.67, 0.10, 0.04, "stitch_gold")
    # The mug, with its handle.
    st("mug", 0.22, 0.62, 0.08, 0.12, "stitch_blue")
    st("mug_handle", 0.28, 0.62, 0.03, 0.07, "stitch_blue")
    st("foam", 0.22, 0.69, 0.08, 0.03, "linen")
    # A line of words under them, and a sun and a moon up in the corners.
    for k in range(9):
        st("word_%d" % k, -0.27 + k * 0.068, 0.45, 0.045, 0.025, "stitch_green" if k % 4 == 3 else "stitch_brown")
    st("sun", -0.28, 0.78, 0.05, 0.05, "stitch_gold")
    st("moon", 0.28, 0.78, 0.05, 0.05, "stitch_blue")
    limb("cord_l", (-0.30, 0.08, 0.92), (0, 0.10, 1.04), 0.010, 0.010, "rope")
    limb("cord_r", (0.30, 0.08, 0.92), (0, 0.10, 1.04), 0.010, 0.010, "rope")
    return centre(1.0)


def prop_cellar_lock():
    """The cellar door behind the bar, shut and locked: heavy planks braced
    and banded with iron in a stone frame, the same size as the inn's doors,
    and on it a brass lock plate with three round slots -- a candle, a loaf
    and a mug stamped by them."""
    blk("jamb_l", (0.16, 0.30, 1.62), (-0.52, 0.04, 0.81), "stone", bev=0.02)
    blk("jamb_r", (0.16, 0.30, 1.62), (0.52, 0.04, 0.81), "stone_pale", bev=0.02)
    blk("lintel", (1.20, 0.32, 0.18), (0, 0.04, 1.70), "stone", bev=0.02)
    blk("sill", (1.00, 0.34, 0.04), (0, 0.0, 0.02), "stone_pale", bev=0.01)
    for k in range(5):
        blk("plank_%d" % k, (0.172, 0.08, 1.56), (-0.344 + k * 0.172, 0.0, 0.80), ("oak", "oak_light")[k % 2],
            bev=0.01)
    for z in (0.28, 1.32):
        blk("band_%d" % int(z * 100), (0.90, 0.10, 0.08), (0, -0.02, z), "iron", metal=0.6, rough=0.5, bev=0.01)
        for sx in (-1, 1):
            sphere("rivet_%d_%d" % (int(z * 100), sx), 0.025, (sx * 0.36, -0.08, z), "iron_light", rough=0.5)
    limb("brace", (-0.36, -0.03, 0.34), (0.36, -0.03, 1.26), 0.035, 0.035, "oak")
    # The lock plate, and its three slots.
    blk("plate", (0.34, 0.04, 0.24), (0.06, -0.06, 0.80), "brass", metal=0.7, rough=0.35, bev=0.01)
    for k, (x, mark) in enumerate(((-0.05, "stitch_gold"), (0.06, "loaf"), (0.17, "pewter"))):
        cyl("slot_%d" % k, 0.040, 0.03, (x, -0.085, 0.82), "void", rot=(math.pi / 2, 0, 0), verts=16)
        sphere("stamp_%d" % k, 0.016, (x, -0.085, 0.75), mark, emit=0.3 if k == 0 else 0.0)
    sphere("ring", 0.03, (0.06, -0.09, 0.68), "brass", rough=0.4)
    blk("draught", (0.84, 0.05, 0.02), (0, -0.04, 0.01), "void", bev=0)
    return 2.1


# =================================================================================
#  Scene 41 -- the Tanner's door, scored
# =================================================================================
def prop_claw_marks():
    """Claw marks to lay over a door frame or a bench: four deep gouges raked
    down at a slant, dark in the groove and splintered pale either side."""
    for k in range(4):
        x = -0.15 + k * 0.10
        a = math.radians(-24)
        ln = 0.42 - 0.05 * abs(k - 1.5)
        z = 0.34 + 0.02 * (k % 2)
        blk("gouge_%d" % k, (0.045, 0.02, ln), (x, 0, z), "void", rot=(0, a, 0), bev=0)
        for side in (-1, 1):
            blk("splinter_%d_%d" % (k, side), (0.018, 0.022, ln * 0.85), (x + side * 0.032 * math.cos(a), -0.005,
                                                                         z - side * 0.032 * math.sin(a)),
                "oak_pale", rot=(0, a, 0), bev=0)
    return centre(0.70)


# =================================================================================
#  Scenes 31, 45 -- the Black Knights fallen
# =================================================================================
bp.PALETTE.update({
    "bk_plate":    (0.170, 0.170, 0.210), "bk_plate_lt": (0.330, 0.330, 0.400),
    "bk_plate_dk": (0.090, 0.090, 0.110), "bk_cloth":    (0.220, 0.160, 0.300),
})


def prop_knight_scrap():
    """What is left of a Black Knight when the violet goes out of its visor: a
    heap of cold black plate with nobody in it -- the bucket helm rolled on
    its side with its slit dark, the breastplate, a pauldron, gauntlets, a
    greave, a rag of its tabard and the broken half of its sword."""
    blk("breast", (0.40, 0.30, 0.16), (0.02, 0.04, 0.10), "bk_plate", rot=(math.radians(-10), 0, 0.2), bev=0.05)
    blk("breast_ridge", (0.06, 0.30, 0.04), (0.02, 0.0, 0.18), "bk_plate_lt", rot=(math.radians(-10), 0, 0.2), bev=0.01)
    blk("tabard", (0.30, 0.34, 0.03), (-0.20, 0.10, 0.02), "bk_cloth", rot=(0, 0, -0.4), bev=0.01)
    sphere("pauldron", 0.14, (-0.24, -0.04, 0.10), "bk_plate").scale = (1.1, 1.0, 0.6)
    ring("pauldron_rim", 0.12, 0.02, (-0.24, -0.04, 0.08), "bk_plate_lt", rot=(0.3, 0.2, 0))
    # The helm on its side, its slit to the camera.
    helm = cyl("helm", 0.12, 0.26, (0.30, -0.10, 0.12), "bk_plate", rot=(0, math.pi / 2, 0.5), verts=16)
    _ = helm
    blk("helm_slit", (0.03, 0.20, 0.03), (0.30, -0.21, 0.13), "bk_plate_dk", rot=(0, 0, 0.5), bev=0)
    sphere("helm_top", 0.12, (0.42, -0.04, 0.12), "bk_plate_lt").scale = (0.3, 1.0, 1.0)
    for k, (x, y, a) in enumerate(((-0.40, -0.16, 0.6), (0.16, 0.20, -0.3))):
        sphere("gauntlet_%d" % k, 0.07, (x, y, 0.06), "bk_plate").scale = (1.4, 1.0, 0.7)
        blk("fingers_%d" % k, (0.10, 0.06, 0.03), (x + 0.08 * math.cos(a), y + 0.08 * math.sin(a), 0.04),
            "bk_plate_lt", rot=(0, 0, a), bev=0.01)
    cyl("greave", 0.07, 0.30, (-0.02, -0.22, 0.07), "bk_plate", rot=(0, math.pi / 2, 0.15), verts=12)
    blk("blade", (0.36, 0.07, 0.02), (0.36, 0.16, 0.03), "iron_light", rot=(0, 0, -0.5), metal=0.6, rough=0.4, bev=0)
    blk("hilt", (0.04, 0.16, 0.04), (0.18, 0.26, 0.04), "bk_plate_dk", rot=(0, 0, -0.5), bev=0.01)
    return 1.10


# =================================================================================
#  Scenes 44-49 -- the Mayor's Hall in the dream, and the Guild's great desk
# =================================================================================
def prop_boo_note():
    """A single note folded once and stood on the desk like a little tent, a
    line of ink showing on the half that faces out."""
    blk("half_a", (0.30, 0.012, 0.18), (0, -0.04, 0.075), "paper", rot=(math.radians(-24), 0, 0), bev=0.004)
    blk("half_b", (0.30, 0.012, 0.18), (0, 0.04, 0.075), "paper", rot=(math.radians(24), 0, 0), bev=0.004)
    blk("ink", (0.12, 0.004, 0.03), (-0.02, -0.08, 0.08), "ink", rot=(math.radians(-24), 0, 0), bev=0)
    return 0.45


def _desk(prefix="", W=1.40, D=0.72):
    blk(prefix + "top", (W, D, 0.08), (0, 0, 0.78), "oak_light", bev=0.015)
    for sx in (-1, 1):
        blk(prefix + "pedestal_%d" % sx, (0.40, D - 0.06, 0.72), (sx * (W / 2 - 0.22), 0, 0.37), "oak", bev=0.015)
        for k in range(3):
            blk(prefix + "drawer_%d_%d" % (sx, k), (0.32, 0.02, 0.18), (sx * (W / 2 - 0.22), -D / 2 + 0.03,
                                                                    0.62 - k * 0.21), "oak_light", bev=0.008)
            sphere(prefix + "knob_%d_%d" % (sx, k), 0.022, (sx * (W / 2 - 0.22), -D / 2 + 0.01, 0.62 - k * 0.21),
                   "brass")
    blk(prefix + "modesty", (W - 0.84, 0.04, 0.40), (0, 0.20, 0.54), "oak", bev=0.01)


def prop_desk_tilted():
    """The Mayor's desk as the Reverie has it: tipped over as if the floor under
    it were a slope, its papers sliding off the low end and the inkwell gone
    over."""
    _desk()
    for k, (x, y, a) in enumerate(((-0.30, 0.0, 10), (0.05, 0.06, -14), (0.34, -0.06, 22))):
        blk("paper_%d" % k, (0.28, 0.36, 0.01), (x, y, 0.83 + k * 0.004), "paper", rot=(0, 0, math.radians(a)),
            bev=0.003)
    cyl("inkwell", 0.05, 0.07, (0.52, 0.12, 0.86), "coal", rot=(math.radians(90), 0, 0.6), verts=12)
    blk("spill", (0.20, 0.12, 0.004), (0.58, 0.02, 0.825), "ink", rot=(0, 0, 0.6), bev=0)
    tip_all("Y", -11, pivot=(0.70, 0.0, 0.0))
    return centre(1.95)


def prop_papers_drift():
    """The Mayor's papers loose in the dream's air: sheets hanging at every
    angle, turning, some written on, nothing holding them up."""
    rnd = random.Random(71)
    for k in range(7):
        x, y, z = rnd.uniform(-0.45, 0.45), rnd.uniform(-0.2, 0.2), rnd.uniform(0.15, 1.10)
        rot = (rnd.uniform(-1.0, 1.0), rnd.uniform(-0.8, 0.8), rnd.uniform(0, 3.0))
        blk("sheet_%d" % k, (0.22, 0.28, 0.008), (x, y, z), "paper", rot=rot, bev=0.002)
        if k % 2 == 0:
            m = Euler(rot, "XYZ").to_matrix()
            for j in range(2):
                o = m @ Vector((0.0, 0.06 - j * 0.08, 0.006))
                blk("line_%d_%d" % (k, j), (0.15, 0.018, 0.004), (x + o.x, y + o.y, z + o.z), "ink", rot=rot, bev=0)
    return centre(1.30)


def prop_great_desk():
    """The Guild master's great desk at the north end of the hall: long, deep
    and heavy, on two pedestals of drawers with a carved panel between, a
    green leather top, a ledger, an inkstand and a brass lamp."""
    W, D = 2.40, 0.92
    blk("top", (W, D, 0.10), (0, 0, 0.80), "oak", bev=0.02)
    blk("leather", (W - 0.50, D - 0.30, 0.012), (0, -0.02, 0.856), "wool_green", rough=0.8, bev=0)
    blk("top_edge", (W + 0.04, D + 0.04, 0.04), (0, 0, 0.74), "oak_light", bev=0.01)
    for sx in (-1, 1):
        blk("pedestal_%d" % sx, (0.62, D - 0.10, 0.72), (sx * (W / 2 - 0.34), 0, 0.36), "oak", bev=0.02)
        blk("plinth_%d" % sx, (0.68, D - 0.04, 0.08), (sx * (W / 2 - 0.34), 0, 0.04), "oak_light", bev=0.01)
        for k in range(3):
            blk("drawer_%d_%d" % (sx, k), (0.52, 0.03, 0.18), (sx * (W / 2 - 0.34), -(D - 0.10) / 2 - 0.01,
                                                              0.58 - k * 0.20), "oak_light", bev=0.01)
            blk("pull_%d_%d" % (sx, k), (0.12, 0.03, 0.03), (sx * (W / 2 - 0.34), -(D - 0.10) / 2 - 0.035,
                                                             0.58 - k * 0.20), "brass", metal=0.7, rough=0.35, bev=0.005)
    blk("panel", (W - 1.36, 0.04, 0.50), (0, -0.16, 0.48), "oak", bev=0.01)
    blk("panel_carve", (W - 1.56, 0.03, 0.30), (0, -0.185, 0.48), "oak_light", bev=0.01)
    sphere("crest", 0.08, (0, -0.20, 0.48), "brass", rough=0.35).scale = (1.0, 0.4, 1.0)
    # On the top: a ledger open, the inkstand, the lamp, a stack of papers.
    blk("ledger_l", (0.30, 0.40, 0.04), (-0.22, -0.04, 0.88), "paper", rot=(0, math.radians(-5), 0), bev=0.005)
    blk("ledger_r", (0.30, 0.40, 0.04), (0.10, -0.04, 0.88), "paper", rot=(0, math.radians(5), 0), bev=0.005)
    blk("ledger_spine", (0.05, 0.42, 0.03), (-0.06, -0.04, 0.87), "cloth_red", bev=0.003)
    blk("inkstand", (0.26, 0.16, 0.05), (0.62, 0.12, 0.885), "brass", metal=0.6, rough=0.4, bev=0.01)
    cyl("ink_pot", 0.04, 0.06, (0.58, 0.12, 0.93), "coal", verts=10)
    blk("quill", (0.02, 0.02, 0.28), (0.66, 0.12, 1.02), "paper", rot=(0, math.radians(-24), 0), bev=0)
    cyl("lamp_foot", 0.08, 0.04, (-0.86, 0.18, 0.88), "brass", metal=0.7, rough=0.35, verts=14)
    cyl("lamp_stem", 0.02, 0.24, (-0.86, 0.18, 1.02), "brass", metal=0.7, rough=0.35, verts=8)
    sphere("lamp_glass", 0.08, (-0.86, 0.18, 1.18), "glass_lit", emit=0.6).scale = (1.0, 1.0, 1.2)
    for k in range(4):
        blk("stack_%d" % k, (0.30, 0.38, 0.02), (0.92, -0.10, 0.88 + k * 0.022), "paper", rot=(0, 0, 0.05 * k), bev=0.003)
    return centre(2.75)


def prop_forge_chimney():
    """Halda's forge's chimney, stood on its flat roof (the shop's art has none):
    a squat stack of stone, wider and blacker about the mouth than a house's, as
    a forge's is. Act I's cold forge is noticed by there being no smoke from it,
    and its smoke says the forge is lit again (World::UpdateChimneys)."""
    blk("flashing", (0.74, 0.60, 0.06), (0, 0, 0.03), "soot", bev=0.01)
    blk("stack", (0.60, 0.50, 0.96), (0, 0, 0.52), "stone", bev=0.02)
    for k in range(3):
        blk("course_%d" % k, (0.62, 0.52, 0.035), (0, 0, 0.24 + k * 0.25), "stone_pale", bev=0.004)
    blk("cap", (0.74, 0.62, 0.11), (0, 0, 1.04), "stone_pale")
    blk("soot_lip", (0.68, 0.56, 0.05), (0, 0, 1.11), "soot")
    blk("mouth", (0.40, 0.30, 0.03), (0, -0.03, 1.14), "char")
    return 1.6


# =================================================================================
#  Wynn's shop and her dream of it -- the thread rack, the dress form, the
#  portrait, her desk and shears, the dream's spool and snarl, her sign
# =================================================================================
# The six threads on her wall. Crimson, ivory and gold are the gown's -- the
# portrait's, the dress form's and the Shear Mannequin's (blender_act1b.py,
# "sm_*"): the same values, which is the puzzle.
THREADS = {
    "crimson": (0.70, 0.10, 0.14), "ivory": (0.93, 0.89, 0.78), "gold": (0.86, 0.66, 0.20),
    "cobalt": (0.16, 0.30, 0.70), "moss": (0.30, 0.52, 0.24), "violet": (0.48, 0.26, 0.62),
}
bp.PALETTE.update({"thr_" + k: v for k, v in THREADS.items()})
bp.PALETTE.update({
    "thr_crimson_dk": (0.50, 0.06, 0.10), "thr_ivory_dk": (0.78, 0.73, 0.62), "thr_gold_dk": (0.64, 0.46, 0.12),
    # The dress form's own cover, a grey-brown canvas: plain enough that the
    # gown going on over it is the change anyone sees.
    "form_canvas": (0.60, 0.56, 0.50), "form_canvas_dk": (0.40, 0.37, 0.33),
    "spool_wood": (0.76, 0.60, 0.40), "spool_wood_dk": (0.36, 0.24, 0.16),
    # The portrait: gilt, a dark green-grey ground the crimson stands out of,
    # and the sitter.
    "gilt": (0.78, 0.60, 0.24), "gilt_dk": (0.50, 0.36, 0.14), "gilt_lt": (0.94, 0.80, 0.42),
    "portrait_bg": (0.16, 0.27, 0.27), "portrait_bg_lt": (0.25, 0.37, 0.36),
    "sitter_skin": (0.92, 0.76, 0.62), "sitter_hair": (0.30, 0.17, 0.11),
    # Where a picture hung: the plaster it kept the sun and the smoke off.
    # Aimed at one step of make_props.ps1's ten paler than the shop's wall
    # (plaster_wall_warm, #d6be96) and the same hue -- it comes out #e3c68e,
    # its edge darkened by the outline into the line of dust round it. Paler
    # than that, it was a blank sheet of paper pinned up.
    "wall_clean": (0.824, 0.722, 0.518), "nail_iron": (0.26, 0.25, 0.27),
    "shear_steel": (0.86, 0.89, 0.94), "shear_steel_dk": (0.46, 0.49, 0.55), "shear_grip": (0.13, 0.12, 0.14),
    "tape_yellow": (0.92, 0.80, 0.38), "pin_head": (0.86, 0.20, 0.20),
    "sign_board": (0.90, 0.85, 0.72), "felt": (0.52, 0.56, 0.50),
})

# The rack's frame, which the strands are rendered in too so that they land
# on its hooks to the pixel: 96 pixels across 2.4 units is forty to the unit,
# and the pairs of hooks are fifteen pixels apart. Six pairs fifteen apart
# cannot all sit a whole number of pixels from the middle of a picture an
# even number wide, so they are half a pixel left of it: each pair's middle
# is then -37, -22, -7, 8, 23 and 38 pixels from the picture's, and a strand
# placed that far along lands on its hooks exactly.
RACK_SPAN, RACK_PX = 2.4, 96
PPU = RACK_PX / RACK_SPAN
SLOT_PX = tuple(-37 + 15 * k for k in range(6))
SLOT_X = tuple(px / PPU for px in SLOT_PX)
HOOK_HALF = 5.0 / PPU               # each hook of a pair from the pair's middle
HOOK_Z = 1.00                       # the pegs' height on the board
CROOK = (-0.062, HOOK_Z + 0.036)    # where a thread rests in a hook: y, z


def _rack_board():
    """The board and its mouldings, with the hooks. Back against the wall at
    +Y; the hooks' pegs stand out toward the room and turn up at the end.
    Each pair is set in a panel of grey-green felt: the two hooks of a pair
    are further apart than one pair's hook is from the next pair's, and with
    nothing between them the eye paired the gaps. The felt is a middling
    grey every one of the six colours stands out from -- on the oak the
    crimson and the cobalt were the board's own darkness."""
    blk("board", (2.26, 0.06, 0.32), (0.0125, 0.075, 1.0), "oak", bev=0.01)
    blk("rail_top", (2.32, 0.11, 0.06), (0.0125, 0.06, 1.185), "oak_light", bev=0.012)
    blk("rail_bottom", (2.32, 0.09, 0.05), (0.0125, 0.065, 0.815), "oak_light", bev=0.012)
    for sx in (-1, 1):
        sphere("screw_%d" % sx, 0.022, (0.0125 + sx * 1.10, 0.040, 1.0), "brass", rough=0.4)
    for k, x0 in enumerate(SLOT_X):
        blk("felt_%d" % k, (2 * HOOK_HALF + 0.03, 0.02, 0.20), (x0, 0.040, HOOK_Z + 0.01), "felt", rough=0.95,
            bev=0.006)
        for side in (-1, 1):
            x = x0 + side * HOOK_HALF
            tag = "%d_%d" % (k, side)
            # Brass, and fat for a hook: a true one is a pixel and vanishes.
            # They throw no shadow: on the felt it was a dark smudge beside
            # every hook, like an eye.
            for ob in (limb("peg_" + tag, (x, 0.04, HOOK_Z), (x, -0.075, HOOK_Z), 0.024, 0.022, "brass"),
                       limb("tip_" + tag, (x, -0.075, HOOK_Z - 0.01), (x, -0.080, HOOK_Z + 0.075), 0.021, 0.018,
                            "brass"),
                       sphere("knob_" + tag, 0.025, (x, -0.080, HOOK_Z + 0.080), "brass", rough=0.4)):
                ob.visible_shadow = False


def prop_thread_rack():
    """The rack on Wynn's back wall: an oak board under a moulding, with six
    pairs of brass hooks in a row -- one pair for each of her threads, every
    pair empty."""
    _rack_board()
    return RACK_SPAN


def _thread_strand(colour):
    """One thread strung taut across a pair of the rack's hooks -- the pair
    whose middle is x=0 -- wound once round each, and from the right-hand
    hook running down to the little spool it came off, hanging under the
    board. Built in the rack's own frame and height, so it lands on any
    pair's hooks when placed that pair's offset along from the rack."""
    c, dk = "thr_" + colour, "spool_wood"
    y, z = CROOK
    # Three pixels thick: two was all outline and no colour.
    limb("strand", (-HOOK_HALF, y, z), (HOOK_HALF, y, z), 0.038, 0.038, c, verts=10)
    for side in (-1, 1):
        ring("wind_%d" % side, 0.030, 0.017, (side * HOOK_HALF, -0.072, HOOK_Z + 0.045), c)
    # The end hanging down off the right hook to its spool.
    sx, sz = HOOK_HALF + 0.035, 0.665
    limb("drop", (HOOK_HALF + 0.006, y, z - 0.01), (sx, -0.07, sz + 0.07), 0.026, 0.024, c, verts=8)
    # The spool lies on its side, its thread toward the camera: a band of
    # the colour between two wooden flanges.
    cyl("spool_thread", 0.072, 0.115, (sx, -0.07, sz), c, rot=(0, math.radians(90), 0), verts=16)
    for side in (-1, 1):
        cyl("spool_flange_%d" % side, 0.088, 0.026, (sx + side * 0.070, -0.07, sz), dk,
            rot=(0, math.radians(90), 0), verts=16)
    return RACK_SPAN


def prop_thread_strand_crimson(): return _thread_strand("crimson")
def prop_thread_strand_ivory():   return _thread_strand("ivory")
def prop_thread_strand_gold():    return _thread_strand("gold")
def prop_thread_strand_cobalt():  return _thread_strand("cobalt")
def prop_thread_strand_moss():    return _thread_strand("moss")
def prop_thread_strand_violet():  return _thread_strand("violet")


def prop_thread_snarl():
    """The rack as the dream has it: buried in a snarl of the Reverie's black
    thread, knots of it all along the board and bulging up over the
    moulding, cords dragged across from end to end, a hook or two still
    showing, and loose ends hanging down its face. The rack's own frame and
    footing, and nothing lower than the rack's own foot -- make_props.ps1
    sits the two by the same amount so the one swaps for the other -- which
    is why the snarl climbs and does not hang: in front of the board, a knot
    low enough to hide its foot would be drawn below it."""
    before = set(bpy.context.scene.objects)
    _rack_board()
    rack = [ob for ob in bpy.context.scene.objects if ob not in before and ob.type == "MESH"]
    # A dark mass along the board for the loops to be wound round: without
    # it they were thin spikes at every angle, a purple thicket.
    for k, x in enumerate((-0.86, -0.42, 0.02, 0.46, 0.92)):
        sphere("snarl_core_%d" % k, 0.20, (x, 0.0, 1.21 + 0.02 * (k % 2)), ("thread", "thread_lt")[k % 2],
               rough=0.4).scale = (1.55, 0.45, 0.72)
    for k, x in enumerate((-0.92, -0.52, -0.12, 0.30, 0.70, 1.02)):
        knot("snarl_%d" % k, (x, -0.03, 1.23 + 0.03 * (k % 2)), 9, (0.13, 0.22), (0.026, 0.040), 4100 + k,
             squash=(1.35, 0.50, 0.85), colours=("thread", "thread_sheen", "thread_lt", "thread"))
    # Cords from end to end, sagging, over and under the knots.
    for k, (z0, z1, sag) in enumerate(((1.22, 1.10, 0.06), (1.06, 1.18, 0.05), (1.30, 1.26, 0.10),
                                      (1.12, 1.02, 0.04))):
        strand("cord_%d" % k, (-1.12, -0.06 - 0.01 * k, z0), (1.14, -0.06 - 0.01 * k, z1), 0.030, 0.024,
               ("thread_lt", "thread", "thread_sheen", "thread")[k], sag=sag, segs=6)
    # Loose ends down the board's face, lying against it.
    for k, x in enumerate((-0.80, -0.30, 0.18, 0.86)):
        strand("end_%d" % k, (x, 0.0, 1.02), (x + 0.05 * (1 - 2 * (k % 2)), 0.02, 0.86), 0.020, 0.014,
               ("thread", "thread_sheen")[k % 2], sag=-0.02, segs=2)
    # Nothing of the snarl may be drawn below the rack's own foot: whatever
    # would be is lifted until it clears it by a pixel.
    floor = _lowest_seen(rack) + 1.0 / PPU
    cos_e = math.cos(math.radians(bp.CAMERA_ELEVATION))
    for ob in bpy.context.scene.objects:
        if ob.type == "MESH" and ob not in rack and ob.parent is None:
            low = _lowest_seen([ob])
            if low < floor:
                ob.location.z += (floor - low) / cos_e
    return RACK_SPAN


def _lowest_seen(objects):
    """How low on the screen the lowest corner of any of these objects is
    drawn, along the props camera's up (bounding boxes, so a little
    generous)."""
    bpy.context.view_layer.update()
    e = math.radians(bp.CAMERA_ELEVATION)
    up = Vector((0.0, math.sin(e), math.cos(e)))
    return min((ob.matrix_world @ Vector(c)).dot(up) for ob in objects for c in ob.bound_box)


# --- the dress form ---------------------------------------------------------------
# blender_props._mannequin's form -- the oak foot, the turned stand, the knob
# on its neck -- with a padded torso of its own that the gown goes on over a
# piece at a time: the bare form, the bodice, the skirt, the sash. Every piece
# keeps above the foot as the camera sees it (the hem well up off the floor),
# so the four pictures sit down by the same amount and swap in place.
DF_TORSO = ((0.001, 1.585), (0.10, 1.585), (0.185, 1.565), (0.232, 1.51), (0.240, 1.43), (0.226, 1.34),
            (0.193, 1.24), (0.168, 1.14), (0.172, 1.06), (0.200, 0.98), (0.214, 0.92), (0.205, 0.86),
            (0.150, 0.83), (0.001, 0.83))
DF_BODICE = ((0.001, 1.598), (0.11, 1.598), (0.197, 1.577), (0.246, 1.518), (0.254, 1.43), (0.240, 1.34),
             (0.207, 1.24), (0.182, 1.14), (0.186, 1.05), (0.205, 0.99), (0.001, 0.99))
DF_SKIRT = ((0.001, 1.09), (0.17, 1.09), (0.198, 1.05), (0.222, 0.98), (0.262, 0.87), (0.31, 0.75),
            (0.36, 0.63), (0.41, 0.51), (0.452, 0.41), (0.476, 0.34), (0.482, 0.31), (0.468, 0.295),
            (0.40, 0.29), (0.001, 0.29))
DF_FLAT = 0.78


def _profile_r(profile, z):
    pts = [p for p in profile if p[0] > 0.01]
    for (r0, z0), (r1, z1) in zip(pts, pts[1:]):
        if (z0 - z) * (z1 - z) <= 0 and z0 != z1:
            return r0 + (r1 - r0) * (z - z0) / (z1 - z0)
    return pts[-1][0]


def _dress_form(bodice=False, skirt=False, sash=False):
    cyl("foot", 0.26, 0.06, (0, 0, 0.03), "oak", verts=24)
    cyl("foot_rim", 0.20, 0.03, (0, 0, 0.075), "oak_light", verts=24)
    cyl("stand", 0.042, 0.80, (0, 0, 0.47), "oak", verts=10)
    cyl("knop", 0.065, 0.05, (0, 0, 0.26), "oak_light", verts=12)
    torso = lathe("torso", DF_TORSO, (0, 0, 0), "form_canvas", rough=0.9)
    torso.scale = (1.0, DF_FLAT, 1.0)
    # The dressmaker's lines on it: a seam down the front and the waist tape.
    for z0, z1 in ((1.54, 1.30), (1.30, 1.06), (1.06, 0.88)):
        r0, r1 = _profile_r(DF_TORSO, z0), _profile_r(DF_TORSO, z1)
        limb("seam_%d" % int(z0 * 100), (0, -r0 * DF_FLAT - 0.004, z0), (0, -r1 * DF_FLAT - 0.004, z1), 0.010,
             0.010, "form_canvas_dk", verts=6)
    tape = ring("waist_tape", 0.172, 0.012, (0, 0, 1.10), "form_canvas_dk")
    tape.scale = (1.0, DF_FLAT, 1.0)
    cyl("neck_cap", 0.085, 0.03, (0, 0, 1.595), "form_wood", verts=16)
    cyl("neck", 0.065, 0.13, (0, 0, 1.665), "form_wood", verts=12)
    sphere("knob", 0.10, (0, 0, 1.79), "form_wood")
    sphere("finial", 0.032, (0, 0, 1.895), "oak_light")
    if bodice:
        b = lathe("bodice", DF_BODICE, (0, 0, 0), "thr_crimson", rough=0.8)
        b.scale = (1.0, DF_FLAT * 1.02, 1.0)
        for sx in (-1, 1):
            sphere("puff_%d" % sx, 0.10, (sx * 0.235, 0, 1.49), "thr_crimson").scale = (1.0, 0.95, 0.85)
    if skirt:
        lathe("skirt", DF_SKIRT, (0, 0, 0), "thr_ivory", rough=0.85)
        ring("hem", 0.474, 0.020, (0, 0, 0.318), "thr_ivory_dk")
        for k in range(5):
            a = math.radians(-90 + (k - 2) * 30)
            pts = [(math.cos(a) * _profile_r(DF_SKIRT, z) * 1.01, math.sin(a) * _profile_r(DF_SKIRT, z) * 1.01, z)
                   for z in (1.00, 0.72, 0.45, 0.33)]
            for i in range(3):
                limb("pleat_%d_%d" % (k, i), pts[i], pts[i + 1], 0.012 + 0.004 * i, 0.014 + 0.004 * i,
                     "thr_ivory_dk", verts=6)
    if sash:
        # Round, and wider than the skirt's top: the skirt is round where the
        # bodice is flattened, and in front of the band it hid it.
        cyl("sash", 0.226, 0.09, (0, 0, 1.065), "thr_gold", verts=24)
        ring("sash_edge", 0.226, 0.010, (0, 0, 1.022), "thr_gold_dk")
        bow = Vector((-0.12, -0.215, 1.07))
        for sx in (-1, 1):
            loop = sphere("bow_%d" % sx, 0.06, tuple(bow + Vector((sx * 0.055, -0.01, 0.012))), "thr_gold")
            loop.scale = (1.25, 0.55, 0.85)
            loop.rotation_euler = (0, math.radians(sx * -28), 0)
        sphere("bow_knot", 0.032, tuple(bow + Vector((0, -0.03, 0))), "thr_gold_dk")
        for k, (a, z) in enumerate(((-122.0, 0.70), (-104.0, 0.78))):
            rr = _profile_r(DF_SKIRT, z) + 0.022
            end = (math.cos(math.radians(a)) * rr, math.sin(math.radians(a)) * rr, z)
            mid_z = (1.04 + z) / 2
            rm = _profile_r(DF_SKIRT, mid_z) + 0.022
            mid = (math.cos(math.radians(a)) * rm, math.sin(math.radians(a)) * rm, mid_z)
            limb("tail_%d_a" % k, tuple(bow + Vector((0, -0.02, -0.02))), mid, 0.028, 0.026, "thr_gold", verts=8)
            limb("tail_%d_b" % k, mid, end, 0.026, 0.024, "thr_gold", verts=8)
    return 2.5


def prop_dress_form_bare():   return _dress_form()
def prop_dress_form_bodice(): return _dress_form(bodice=True)
def prop_dress_form_skirt():  return _dress_form(bodice=True, skirt=True)
def prop_dress_form_full():   return _dress_form(bodice=True, skirt=True, sash=True)


# --- the portrait, and where it hung ------------------------------------------------
# Both in one frame -- sixty pixels across 1.5 units, forty to the unit,
# the thread rack's scale -- with the nail in the same place, and sat down by
# the same amount (make_props.ps1's $ALIGN), so taking the picture down shows
# the clean plaster it covered exactly where it was.
PORTRAIT_SPAN = 1.50
PF_W, PF_H, PF_Z0 = 1.08, 1.36, -0.20          # the frame: outer width, height, bottom (low, to fit the nail in)
PF_NAIL = (0.0, 0.006, PF_Z0 + PF_H + 0.17)     # the nail it hangs from


def _nail():
    limb("nail_shank", (PF_NAIL[0], 0.03, PF_NAIL[2]), (PF_NAIL[0], PF_NAIL[1] - 0.02, PF_NAIL[2] + 0.004), 0.010,
         0.010, "nail_iron", verts=6)
    cyl("nail_head", 0.026, 0.012, (PF_NAIL[0], PF_NAIL[1] - 0.024, PF_NAIL[2] + 0.004), "nail_iron",
        rot=(math.radians(90), 0, 0), verts=12)


def prop_portrait_gown():
    """The portrait in Wynn's dream shop: a woman standing in the gown, full
    length, against a dark green-grey ground -- a crimson bodice with puffed
    sleeves, a gold sash at the waist with its bow and tails, a full ivory
    skirt to the floor -- in a gilt frame on a cord from a nail. Three blocks
    of colour, one over the other, big enough to be the first thing seen."""
    z0, w, h = PF_Z0, PF_W, PF_H
    m = 0.10                                   # the moulding's width
    cz = z0 + h / 2
    # The gilt frame: four mitred bars, an inner lip, a bead at each corner.
    for name, size, loc in (("top", (w, 0.07, m), (0, -0.005, z0 + h - m / 2)),
                            ("bottom", (w, 0.07, m), (0, -0.005, z0 + m / 2)),
                            ("left", (m, 0.07, h - 2 * m), (-w / 2 + m / 2, -0.005, cz)),
                            ("right", (m, 0.07, h - 2 * m), (w / 2 - m / 2, -0.005, cz))):
        blk("frame_" + name, size, loc, "gilt", rough=0.45, metal=0.5, bev=0.02)
    for name, size, loc in (("lip_t", (w - 2 * m, 0.03, 0.03), (0, -0.025, z0 + h - m - 0.012)),
                            ("lip_b", (w - 2 * m, 0.03, 0.03), (0, -0.025, z0 + m + 0.012)),
                            ("lip_l", (0.03, 0.03, h - 2 * m), (-w / 2 + m + 0.012, -0.025, cz)),
                            ("lip_r", (0.03, 0.03, h - 2 * m), (w / 2 - m - 0.012, -0.025, cz))):
        blk(name, size, loc, "gilt_dk", rough=0.5, metal=0.4, bev=0)
    for sx in (-1, 1):
        for sz in (-1, 1):
            sphere("corner_%d_%d" % (sx, sz), 0.042, (sx * (w / 2 - m / 2), -0.045, cz + sz * (h / 2 - m / 2)),
                   "gilt_lt", rough=0.4)
    sphere("crest", 0.05, (0, -0.045, z0 + h - m / 2), "gilt_lt", rough=0.4).scale = (1.4, 0.6, 0.9)
    # The canvas, and on it the sitter.
    cw, ch = w - 2 * m, h - 2 * m
    blk("canvas", (cw, 0.02, ch), (0, 0.010, cz), "portrait_bg", rough=0.95, bev=0)
    paint = lambda name, pts, colour, y=-0.004: bp._cloth_shape(name, pts, y, 0.006, colour)  # noqa: E731
    paint("glow", [(-0.26, z0 + m + 0.10), (0.26, z0 + m + 0.10), (0.24, z0 + h - m - 0.08),
                   (-0.24, z0 + h - m - 0.08)], "portrait_bg_lt", y=-0.002)
    paint("floor", [(-cw / 2, z0 + m), (cw / 2, z0 + m), (cw / 2, z0 + m + 0.07), (-cw / 2, z0 + m + 0.07)],
          "sitter_hair", y=-0.002)
    base, waist, shoulder = z0 + m + 0.04, z0 + 0.66, z0 + 0.94
    # The skirt: wide at the hem, belled out over the floor.
    paint("skirt", [(-0.30, base), (0.30, base), (0.27, base + 0.10), (0.17, waist - 0.02), (-0.17, waist - 0.02),
                    (-0.27, base + 0.10)], "thr_ivory")
    for k, x in enumerate((-0.14, -0.04, 0.06, 0.15)):
        paint("fold_%d" % k, [(x - 0.012, base + 0.02), (x + 0.012, base + 0.02), (x * 0.45 + 0.006, waist - 0.04),
                              (x * 0.45 - 0.006, waist - 0.04)], "thr_ivory_dk", y=-0.006)
    # The bodice, puffed at the shoulder.
    paint("bodice", [(-0.11, waist - 0.02), (0.11, waist - 0.02), (0.14, waist + 0.12), (0.16, shoulder),
                     (-0.16, shoulder), (-0.14, waist + 0.12)], "thr_crimson")
    for sx in (-1, 1):
        paint("puff_%d" % sx, [(sx * 0.13, shoulder - 0.10), (sx * 0.22, shoulder - 0.08), (sx * 0.23, shoulder - 0.01),
                               (sx * 0.15, shoulder + 0.02)], "thr_crimson")
        # Her arms down her sides to her hands, folded at the sash.
        paint("arm_%d" % sx, [(sx * 0.17, shoulder - 0.09), (sx * 0.215, shoulder - 0.08), (sx * 0.17, waist + 0.02),
                              (sx * 0.08, waist - 0.01), (sx * 0.07, waist + 0.04), (sx * 0.13, waist + 0.06)],
              "sitter_skin", y=-0.007)
    # The sash, its bow on her left hip and the tails down the skirt.
    paint("sash", [(-0.125, waist - 0.05), (0.125, waist - 0.05), (0.125, waist + 0.02), (-0.125, waist + 0.02)],
          "thr_gold", y=-0.008)
    paint("bow", [(-0.17, waist - 0.07), (-0.07, waist - 0.01), (-0.17, waist + 0.05)], "thr_gold", y=-0.009)
    paint("tail_a", [(-0.12, waist - 0.04), (-0.09, waist - 0.04), (-0.15, waist - 0.26), (-0.18, waist - 0.25)],
          "thr_gold", y=-0.009)
    paint("tail_b", [(-0.10, waist - 0.04), (-0.08, waist - 0.04), (-0.08, waist - 0.20), (-0.11, waist - 0.20)],
          "thr_gold_dk", y=-0.009)
    # Neck and head, her hair put up.
    paint("neck", [(-0.035, shoulder - 0.01), (0.035, shoulder - 0.01), (0.03, shoulder + 0.06), (-0.03, shoulder + 0.06)],
          "sitter_skin", y=-0.007)
    paint("face", [(-0.065, shoulder + 0.05), (0.065, shoulder + 0.05), (0.075, shoulder + 0.14), (0.05, shoulder + 0.19),
                   (-0.05, shoulder + 0.19), (-0.075, shoulder + 0.14)], "sitter_skin", y=-0.007)
    paint("hair", [(-0.085, shoulder + 0.12), (-0.06, shoulder + 0.21), (0.0, shoulder + 0.23), (0.06, shoulder + 0.21),
                   (0.085, shoulder + 0.12), (0.06, shoulder + 0.17), (-0.06, shoulder + 0.17)], "sitter_hair", y=-0.008)
    paint("bun", [(-0.04, shoulder + 0.21), (0.04, shoulder + 0.21), (0.035, shoulder + 0.27), (-0.035, shoulder + 0.27)],
          "sitter_hair", y=-0.008)
    # The cord up to the nail.
    for sx in (-1, 1):
        limb("cord_%d" % sx, (sx * (w / 2 - 0.16), -0.01, z0 + h - 0.02), (PF_NAIL[0] + sx * 0.02, PF_NAIL[1] - 0.012,
                                                                       PF_NAIL[2] - 0.01), 0.011, 0.011, "rope", verts=6)
    _nail()
    return PORTRAIT_SPAN


def prop_nail_patch():
    """Where the portrait hung: a rectangle of plaster paler and cleaner than
    the wall round it, the frame's own size and place, and the empty nail
    over it."""
    blk("patch", (PF_W, 0.004, PF_H), (0, 0.028, PF_Z0 + PF_H / 2), "wall_clean", rough=0.95, bev=0)
    _nail()
    return PORTRAIT_SPAN


# --- Wynn's desk, her shears, the dream's spool, her sign, a door knocked flat ------
DESK_SPAN, DESK_PX = 2.6, 80
DESK_H = 0.80
SHEARS_AT = (0.50, -0.07)          # where on the desk's top the shears are laid


def prop_sewing_desk():
    """Wynn's desk: a long oak table with two drawers, and on it what she
    works with -- bolts of cloth stacked at one end, a fat red pincushion
    stuck with pins, her tape unrolled across the top and over the front
    edge, a couple of spools -- and at the other end a bare stretch of the
    top where her shears lie when they are not in her hand (shears_lying)."""
    W, D, H = 1.80, 0.80, DESK_H
    blk("top", (W, D, 0.07), (0, 0, H - 0.035), "oak_light", bev=0.015)
    blk("apron", (W - 0.14, 0.04, 0.15), (0, -D / 2 + 0.05, H - 0.145), "oak", bev=0.008)
    for sx in (-1, 1):
        blk("drawer_%d" % sx, (0.56, 0.02, 0.10), (sx * 0.42, -D / 2 + 0.025, H - 0.145), "oak_light", bev=0.008)
        sphere("pull_%d" % sx, 0.022, (sx * 0.42, -D / 2 + 0.005, H - 0.145), "brass", rough=0.4)
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.09, 0.09, H - 0.07), (sx * (W / 2 - 0.08), sy * (D / 2 - 0.08), (H - 0.07) / 2),
                "oak", bev=0.012)
    blk("stretcher", (W - 0.22, 0.05, 0.05), (0, 0.0, 0.16), "oak", bev=0.008)
    # Bolts of cloth at the left end, folded flat and stacked, and one on a
    # roll behind them.
    blk("bolt_a", (0.46, 0.34, 0.09), (-0.58, 0.03, H + 0.045), "fab_blue", bev=0.03)
    blk("bolt_b", (0.42, 0.30, 0.08), (-0.57, 0.05, H + 0.13), "fab_rose", rot=(0, 0, math.radians(7)), bev=0.03)
    blk("bolt_c", (0.38, 0.28, 0.07), (-0.59, 0.04, H + 0.205), "fab_teal", rot=(0, 0, math.radians(-4)), bev=0.03)
    cyl("roll", 0.075, 0.62, (-0.50, 0.28, H + 0.075), "fab_green", rot=(0, math.radians(90), 0), verts=14)
    for sx in (-1, 1):
        cyl("roll_end_%d" % sx, 0.03, 0.64, (-0.50, 0.28, H + 0.075), "oak_pale", rot=(0, math.radians(90), 0), verts=8)
    # The pincushion, a fat red tomato with pins in it.
    cushion = sphere("pincushion", 0.095, (-0.08, -0.10, H + 0.058), "fab_red", rough=0.9)
    cushion.scale = (1.0, 1.0, 0.68)
    sphere("cushion_cap", 0.03, (-0.08, -0.10, H + 0.125), "leaf")
    for k, (dx, dy, c) in enumerate(((-0.05, -0.04, "pewter"), (0.04, -0.05, "brass"), (0.06, 0.03, "pewter"),
                                     (-0.03, 0.05, "stitch_gold"), (0.0, -0.08, "pin_head"))):
        limb("pin_%d" % k, (-0.08 + dx * 0.8, -0.10 + dy * 0.8, H + 0.09), (-0.08 + dx * 1.3, -0.10 + dy * 1.3, H + 0.15),
             0.006, 0.006, "pewter", verts=5)
        sphere("pinhead_%d" % k, 0.017, (-0.08 + dx * 1.3, -0.10 + dy * 1.3, H + 0.155), c)
    # Her tape: unrolled across the top and over the front edge, its coil
    # by the spools.
    blk("tape", (0.86, 0.06, 0.008), (0.06, -0.28, H + 0.004), "tape_yellow", rot=(0, 0, math.radians(3)), bev=0)
    blk("tape_hang", (0.06, 0.008, 0.20), (0.48, -D / 2 - 0.006, H - 0.09), "tape_yellow", bev=0)
    cyl("tape_coil", 0.065, 0.035, (0.22, 0.20, H + 0.018), "tape_yellow", verts=16)
    cyl("tape_hub", 0.022, 0.04, (0.22, 0.20, H + 0.02), "oak_pale", verts=10)
    # Two spools at the back, out of the shears' way.
    for k, (x, y, c) in enumerate(((0.62, 0.26, "fab_purple"), (0.78, 0.20, "fab_gold"))):
        cyl("spool_%d" % k, 0.045, 0.10, (x, y, H + 0.05), c, verts=12)
        for z in (0.005, 0.095):
            cyl("spool_%d_end_%d" % (k, int(z * 1000)), 0.058, 0.014, (x, y, H + z), "spool_wood", verts=12)
    # A scrap of chalk and a few snippets of thread by where the shears go.
    blk("chalk", (0.10, 0.04, 0.025), (0.70, -0.24, H + 0.012), "paper", rot=(0, 0, math.radians(20)), bev=0.004)
    return DESK_SPAN


SHEARS_PX = 28


def prop_shears_lying():
    """Wynn's long dressmaker's shears laid flat: two long bright blades
    shut together to a point, the screw at the pivot, the black bows -- one
    small for the thumb, one long for the fingers. Drawn to the desk's scale
    (about thirty-one pixels to the unit), lying a little askew. Twice as
    broad in the blade as real shears: lying flat under a camera thirty-four
    degrees up, a true blade is one pixel high and reads as a crack in the
    table."""
    z = 0.016
    # The blades: two long flat wedges side by side, the near one bright,
    # the far one showing its darker back.
    for k, (colour, off, w) in enumerate((("shear_steel", -0.030, 0.056), ("shear_steel_dk", 0.034, 0.050))):
        blade = cone("blade_%d" % k, w, 0.50, (0.25, off, z + 0.006 * (1 - k)), colour, rot=(0, math.radians(90), 0),
                     verts=16)
        blade.scale = (0.28, 1.0, 1.0)      # the cone's own X is the world's Z once it is laid along X
    cyl("pivot", 0.030, 0.03, (0.0, 0.0, z + 0.014), "brass", verts=12)
    # The shanks out to the bows.
    limb("shank_a", (0.0, 0.02, z), (-0.15, 0.085, z), 0.022, 0.020, "shear_grip", verts=8)
    limb("shank_b", (0.0, -0.02, z), (-0.14, -0.090, z), 0.022, 0.020, "shear_grip", verts=8)
    ring("bow_thumb", 0.060, 0.022, (-0.20, 0.10, z), "shear_grip")
    big = ring("bow_fingers", 0.068, 0.022, (-0.22, -0.11, z), "shear_grip")
    big.scale = (1.40, 1.0, 1.0)
    turn_all(-10)
    return SHEARS_PX / (DESK_PX / DESK_SPAN)


def prop_giant_spool():
    """A spool of the Reverie's black thread as big as a cask, hanging from
    a rafter on a cord through its spindle-hole: dark wooden flanges, the
    thread wound thick between them and catching the violet sheen in bands,
    and a loose end of it hanging down and curling."""
    zc = 1.0
    for k, z in enumerate((zc - 0.40, zc + 0.40)):
        cyl("flange_%d" % k, 0.50, 0.08, (0, 0, z), "spool_wood_dk", verts=32, rough=0.6)
        ring("flange_rim_%d" % k, 0.49, 0.022, (0, 0, z), "spool_wood", rough=0.6)
    cyl("thread_body", 0.40, 0.72, (0, 0, zc), "thread", verts=32, rough=0.35)
    # The windings: bands of the sheen and of the lighter black, round and
    # round it.
    for k in range(9):
        z = zc - 0.32 + k * 0.08
        ring("wind_%d" % k, 0.402, 0.016, (0, 0, z), ("thread_sheen", "thread_lt", "thread")[k % 3], rough=0.3)
    cyl("hole", 0.07, 0.012, (0, 0, zc + 0.442), "void", verts=16)
    # The cord, knotted on top and running up out of sight.
    sphere("knot", 0.045, (0, 0, zc + 0.47), "rope")
    limb("cord", (0, 0, zc + 0.46), (0, 0.04, zc + 1.30), 0.026, 0.024, "rope", verts=8)
    # The loose end, off the front of the body and down, curling.
    strand("loose_a", (0.30, -0.27, zc - 0.10), (0.42, -0.30, zc - 0.62), 0.024, 0.020, "thread", sag=-0.04, segs=3)
    strand("loose_b", (0.42, -0.30, zc - 0.62), (0.34, -0.32, zc - 0.80), 0.020, 0.012, "thread_sheen", sag=0.02, segs=2)
    return centre(2.2)


def prop_clothier_sign():
    """Wynn's sign, for the front of her house: an iron bracket out from the
    wall with a scroll under its arm, and a board hung from it on two rings,
    painted cream with a pair of open shears and a spool of crimson thread."""
    blk("wall_plate", (0.07, 0.04, 0.32), (-0.58, 0.06, 1.08), "iron", bev=0.01)
    limb("arm", (-0.58, 0.03, 1.20), (0.50, 0.03, 1.20), 0.026, 0.022, "iron")
    sphere("arm_end", 0.036, (0.51, 0.03, 1.20), "iron")
    # The scroll under the arm, a quarter turn from the plate up to the arm.
    pts = [(-0.58 + 0.30 * (1 - math.cos(a)), 0.03, 0.92 + 0.28 * math.sin(a)) for a in
           (math.radians(d) for d in (0, 22, 45, 68, 90))]
    for k in range(4):
        limb("scroll_%d" % k, pts[k], pts[k + 1], 0.018, 0.018, "iron", verts=8)
    ring("curl", 0.045, 0.014, (-0.53, 0.03, 0.93), "iron", rot=(math.radians(90), 0, 0))
    # Hung from two rings.
    for sx in (-1, 1):
        ring("hanger_%d" % sx, 0.034, 0.010, (sx * 0.30, 0.03, 1.155), "iron", rot=(math.radians(90), 0, 0))
        limb("hook_%d" % sx, (sx * 0.30, 0.03, 1.12), (sx * 0.30, 0.03, 1.08), 0.010, 0.010, "iron", verts=6)
    blk("board_frame", (0.90, 0.05, 0.54), (0, 0.03, 0.81), "shop_timber", bev=0.012)
    blk("board", (0.80, 0.05, 0.44), (0, 0.012, 0.81), "sign_board", rough=0.85, bev=0.006)
    y = -0.016

    def paint(name, pts, colour):
        bp._cloth_shape(name, pts, y, 0.008, colour)
    # The shears, open, on the left half: two blades crossing at the screw,
    # and their bows below.
    px, pz = -0.18, 0.80
    for sx in (-1, 1):
        paint("shear_blade_%d" % sx, [(px, pz - 0.01), (px + sx * 0.025, pz + 0.005), (px + sx * 0.13, pz + 0.17),
                                      (px + sx * 0.10, pz + 0.18)], "shear_grip")
        paint("shear_shank_%d" % sx, [(px, pz + 0.01), (px + sx * 0.02, pz - 0.005), (px - sx * 0.06, pz - 0.09),
                                      (px - sx * 0.08, pz - 0.08)], "shear_grip")
        paint("shear_bow_%d" % sx, [(px - sx * 0.04, pz - 0.08), (px - sx * 0.12, pz - 0.08), (px - sx * 0.13, pz - 0.15),
                                    (px - sx * 0.08, pz - 0.18), (px - sx * 0.04, pz - 0.15)], "shear_grip")
    # The spool on the right half: crimson thread between gold flanges, its
    # end trailing.
    sx0 = 0.17
    paint("spool_thread", [(sx0 - 0.065, 0.72), (sx0 + 0.065, 0.72), (sx0 + 0.065, 0.90), (sx0 - 0.065, 0.90)], "thr_crimson")
    for z in (0.69, 0.90):
        paint("spool_flange_%d" % int(z * 100), [(sx0 - 0.11, z), (sx0 + 0.11, z), (sx0 + 0.11, z + 0.035),
                                                 (sx0 - 0.11, z + 0.035)], "thr_gold")
    paint("spool_end", [(sx0 + 0.065, 0.80), (sx0 + 0.07, 0.78), (sx0 + 0.17, 0.66), (sx0 + 0.16, 0.65)], "thr_crimson")
    return centre(1.30)


def prop_door_fallen():
    """A plain ledged house door lying where it fell, knocked off its
    hinges, face down: five boards, and on its back the two ledges and the
    brace between them -- the Z that says door from above, where its face
    was only boards -- the iron straps of its hinges along the edge that
    hung, one torn off at the knuckle and bent up, and splinters where they
    came out of the frame. Laid across the floor a little askew."""
    L, Wd, T = 1.90, 0.92, 0.06
    n = 5
    for k in range(n):
        y = -Wd / 2 + (k + 0.5) * Wd / n
        blk("plank_%d" % k, (L, Wd / n - 0.012, T), (0, y, T / 2), ("oak_light", "oak_pale")[k % 2], bev=0.012)
    xl = 0.64                                   # the ledges, either end
    for k, x in enumerate((-xl, xl)):
        blk("ledge_%d" % k, (0.15, Wd - 0.10, 0.05), (x, 0.0, T + 0.025), "oak", bev=0.012)
    # The brace from the hinge end of one ledge up to the far end of the other.
    a, b = Vector((-xl + 0.06, -Wd / 2 + 0.12, 0)), Vector((xl - 0.06, Wd / 2 - 0.12, 0))
    d = b - a
    blk("brace", (d.length, 0.13, 0.045), (0.0, 0.0, T + 0.022), "oak", rot=(0, 0, math.atan2(d.y, d.x)), bev=0.012)
    # The hinge straps along the ledges from the hinged edge, nailed through.
    for k, x in enumerate((-xl, xl)):
        blk("strap_%d" % k, (0.07, 0.50, 0.014), (x, -Wd / 2 + 0.25, T + 0.056), "iron", bev=0.005)
        for j in range(3):
            sphere("nail_%d_%d" % (k, j), 0.016, (x, -Wd / 2 + 0.08 + j * 0.17, T + 0.066), "iron_light")
        cyl("knuckle_%d" % k, 0.034, 0.12, (x, -Wd / 2 - 0.02, T / 2 + 0.01), "iron", rot=(0, math.radians(90), 0),
            verts=10)
    blk("strap_torn", (0.07, 0.15, 0.014), (xl, -Wd / 2 - 0.08, T + 0.05), "iron", rot=(math.radians(-38), 0, 0),
        bev=0.005)
    for k, (x, a_) in enumerate(((-0.74, 20), (-0.55, -15), (0.74, 30), (0.52, -25))):
        blk("splinter_%d" % k, (0.12, 0.025, 0.02), (x, -Wd / 2 - 0.03, T - 0.01), "oak_pale",
            rot=(0, 0, math.radians(a_)), bev=0)
    turn_all(9)
    return 2.2


# =================================================================================
#  Act II -- the net Vexel hangs the old man in, the Infernal Pit's rune and
#  rubble throne, Hoarfang's head for a wall, the lizardmen's cave and the
#  cauldron gone cold
# =================================================================================

# The character sheets' camera: 46 degrees up, a 64 pixel frame 3.5 units
# across (blender_character.py, CAMERA_ELEVATION and FRAME_SPAN). The net is
# rendered from it, at its scale, so it fits the figure it is laid over.
CHAR_ELEVATION, CHAR_SPAN, CHAR_PX = 46.0, 3.5, 64


def _seen_rows(span, px, elevation, objects=None):
    """The highest and lowest rows, in a `px` square picture of `span` units
    seen from `elevation` degrees (setup_camera's framing), that any vertex
    of these objects -- everything meshed, by default -- is drawn at."""
    bpy.context.view_layer.update()
    e = math.radians(elevation)
    up = Vector((0.0, math.sin(e), math.cos(e)))
    target = Vector((0.0, 0.0, span * 0.34))
    ppu = px / span
    rows = []
    for ob in (objects if objects is not None else bpy.context.scene.objects):
        if ob.type != "MESH":
            continue
        m = ob.matrix_world
        for v in ob.data.vertices:
            rows.append(px / 2.0 - ((m @ v.co) - target).dot(up) * ppu)
    return min(rows), max(rows)


def _cord(name, pts, r, colour, rough=0.35):
    """A thread through the points: tapered lengths end to end, with a bead at
    each bend so the corners close."""
    pts = [Vector(p) for p in pts]
    for i in range(len(pts) - 1):
        ob = limb("%s_%d" % (name, i), pts[i], pts[i + 1], r, r, colour, verts=8)
        ob.data.materials[0] = material(name, colour, rough)
    for i in range(1, len(pts) - 1):
        sphere("%s_j%d" % (name, i), r, tuple(pts[i]), colour, rough=rough)


# The net round the old man. Everything is placed against the magister's own
# picture: his soles are the bottom row of his 64 pixel frame's figure (row
# 47, six above the sheet's anchor at 54), the collar under his chin is row
# 36, and his hair is twelve pixels wide about the middle. A ring round a
# body seen from 46 degrees up is drawn lower at its front than at its sides,
# so the bag's rims are set by where their fronts land, not by how high they
# are: the top rim's front under the chin, the bottom rim's front at the
# soles, which makes it the lowest thing in the picture -- the row the
# picture is sat down on.
NET_TOP, NET_BOT = 1.06, 0.22          # the rims' heights
NET_R_TOP, NET_R_MID, NET_R_BOT = 0.40, 0.44, 0.31
NET_STRAND = 0.056                     # two pixels across at 18.3 to the unit


def _net_r(z):
    """The bag's radius at height z: full in the middle, drawn in at the
    rims like a lantern's."""
    f = (z - NET_BOT) / (NET_TOP - NET_BOT)
    if f < 0.45:
        g = f / 0.45
        return NET_R_BOT + (NET_R_MID - NET_R_BOT) * math.sin(g * math.pi / 2)
    g = (f - 0.45) / 0.55
    return NET_R_MID + (NET_R_TOP - NET_R_MID) * (1 - math.cos(g * math.pi / 2))


def _net_at(theta_deg, z):
    t = math.radians(theta_deg)
    r_ = _net_r(z)
    return (r_ * math.cos(t), r_ * math.sin(t), z)


def prop_thread_net():
    """A net of the Reverie's black thread that a standing man hangs in, held
    up like a lantern: a loose diamond mesh round him from the shoulders to
    below the knees, drawn in at both rims, and three strands running up out
    of the top of the picture. Drawn IN FRONT of the magister's sprite with
    its bottom-centre at his soles, so only the half of the bag toward the
    camera is built -- the half behind him would be drawn over him too --
    and the gaps between the strands are empty, so he shows through."""
    colours = ("thread_sheen", "thread", "thread_lt")
    # The mesh: two families of threads winding round the front half of the
    # bag (theta 180 to 360, the -Y side), one up to the right and one up to
    # the left, so they cross in diamonds about seven pixels across. Five
    # pixels apart, the strands were most of the picture and he was a dark
    # blob behind them.
    step, slope = 60.0, 104.0                 # degrees apart; degrees turned per unit risen
    lo, hi = 176.0, 364.0                     # a little round the sides, where the bag turns away
    n = 0
    for fam, sgn in (("ur", 1.0), ("ul", -1.0)):
        for k in range(-3, 6):
            base = 180.0 + k * step + (step / 2 if sgn < 0 else 0.0)
            pts = []
            for i in range(13):
                z = NET_BOT + (NET_TOP - NET_BOT) * i / 12
                th = base + sgn * slope * (z - NET_BOT)
                if lo <= th <= hi:
                    pts.append(_net_at(th, z))
            if len(pts) >= 2:
                _cord("mesh_%s_%d" % (fam, k), pts, NET_STRAND, colours[n % len(colours)])
                n += 1
    # The rims: a drawstring round the top, a hem round the bottom.
    for tag, z, colour in (("top", NET_TOP, "thread_lt"), ("bot", NET_BOT, "thread_sheen")):
        pts = [_net_at(180.0 + 10.0 * i, z) for i in range(19)]
        _cord("rim_" + tag, pts, NET_STRAND, colour)
    # Up out of the picture: one from each side of the drawstring, past his
    # ears, and a third from its front, off to the right of his head.
    zf = 7.0
    # Not the plain black: on the Reverie's dark floors that was no line at all.
    for k, (a, top, colour) in enumerate(((180.0, (-0.36, 0.06, zf), "thread_sheen"),
                                          (360.0, (0.37, 0.04, zf), "thread_lt"),
                                          (318.0, (1.25, -0.10, zf), "thread_sheen"))):
        _cord("anchor_%d" % k, [_net_at(a, NET_TOP), top], NET_STRAND, colour)
    # Sat so the bag's lowest edge is the picture's bottom row, read off the
    # geometry rather than guessed: make_props.ps1 sits a picture on its
    # lowest pixel, and anything under it would pull the strands off the top.
    _, low = _seen_rows(CHAR_SPAN, CHAR_PX, CHAR_ELEVATION)
    dz = (low - (CHAR_PX - 0.40)) / (CHAR_PX / CHAR_SPAN) / math.cos(math.radians(CHAR_ELEVATION))
    for ob in bpy.context.scene.objects:
        if ob.parent is None and ob.type in {"MESH", "EMPTY"}:
            ob.location.z += dz
    e = math.radians(CHAR_ELEVATION)
    feet = CHAR_PX / 2.0 - (dz - CHAR_SPAN * 0.34) * math.cos(e) * (CHAR_PX / CHAR_SPAN)
    print("  thread_net: bag sat down by %.3f units; his feet's floor point lands at row %.2f" % (dz, feet))
    return (CHAR_SPAN, CHAR_ELEVATION)


# --- the Infernal Pit -----------------------------------------------------------
bp.PALETTE.update({
    # Black volcanic stone: nearly black, a little warm, and a grey that
    # catches the light along its edges so it has a shape at all.
    "pit_stone":    (0.135, 0.120, 0.135), "pit_stone_dk": (0.075, 0.068, 0.080),
    "pit_stone_lt": (0.270, 0.245, 0.265),
    # The rune's light: pale, steady, white-blue -- and a dim blue where it
    # spills into the groove, so the carving reads lit and not painted on.
    "rune_white":   (0.760, 0.880, 1.000), "rune_spill":   (0.240, 0.340, 0.500),
    "cinder":       (0.200, 0.170, 0.160), "cinder_lt":    (0.330, 0.290, 0.270),
    "ember_dim":    (0.900, 0.360, 0.120),
})

# The rune, as strokes in a square from -1 to 1 (x across, z up): a stem with
# two arms raised from it, stood on an open diamond. The stem stops at the
# diamond: run through it, the diamond closed up into a white blob.
# tools/make_act2_icons.py draws the rubbing of it from the same strokes.
PIT_RUNE = ((0.0, 0.05, 0.0, 1.0),
            (0.0, 0.40, -0.70, 0.98), (0.0, 0.40, 0.70, 0.98),
            (0.0, 0.05, 0.62, -0.45), (0.62, -0.45, 0.0, -0.95),
            (0.0, -0.95, -0.62, -0.45), (-0.62, -0.45, 0.0, 0.05))


def _rune_strokes(prefix, cx, y, cz, w, h, width, colour, emit):
    """PIT_RUNE cut into a face at y, centred (cx, cz), w by h across."""
    for k, (x0, z0, x1, z1) in enumerate(PIT_RUNE):
        a = Vector((cx + x0 * w / 2, y, cz + z0 * h / 2))
        b = Vector((cx + x1 * w / 2, y, cz + z1 * h / 2))
        d = b - a
        blk("%s_%d" % (prefix, k), (d.length + width, 0.03, width), tuple((a + b) / 2), colour,
            rot=(0, -math.atan2(d.z, d.x), 0), emit=emit, rough=0.6, bev=0)


def prop_pit_rune():
    """A slab of black volcanic stone stood against a wall of the Infernal
    Pit, its top broken off ragged, with a rune carved into its face that
    glows a pale, steady white-blue -- unlike the hellfire round it. Lit low
    (emission about 1.5) and in strokes two pixels wide, so it is a lit
    carving and not a white blob; the groove round each stroke glows dimmer."""
    T = 0.24
    blk("slab", (0.80, T, 0.98), (0.0, 0.0, 0.49), "pit_stone", bev=0.04)
    # The broken top: a shoulder left standing on one side, a lower one on
    # the other, and the break between them.
    blk("crown_l", (0.46, T - 0.02, 0.26), (-0.17, 0.0, 1.04), "pit_stone", rot=(0, math.radians(6), 0), bev=0.04)
    blk("crown_r", (0.30, T - 0.03, 0.14), (0.24, 0.005, 0.99), "pit_stone_lt", rot=(0, math.radians(-14), 0),
        bev=0.03)
    # Seams and spalls in the face, darker and lighter.
    blk("seam", (0.04, 0.02, 0.36), (0.33, -T / 2 - 0.002, 0.30), "pit_stone_dk", rot=(0, math.radians(12), 0), bev=0)
    blk("spall", (0.16, 0.03, 0.10), (-0.30, -T / 2 - 0.004, 0.86), "pit_stone_lt", rot=(0, math.radians(-20), 0),
        bev=0.01)
    # The rune: the dim spill first, wider and a hair behind, then the strokes.
    _rune_strokes("spill", 0.0, -T / 2 - 0.004, 0.53, 0.54, 0.72, 0.095, "rune_spill", 0.5)
    _rune_strokes("rune", 0.0, -T / 2 - 0.012, 0.53, 0.54, 0.72, 0.055, "rune_white", 1.3)
    # Its foot in cinders and broken stone.
    rng = random.Random(77)
    for k in range(9):
        x = rng.uniform(-0.55, 0.55)
        y = rng.uniform(-0.30, -0.10)
        s = rng.uniform(0.06, 0.12)
        blk("rubble_%d" % k, (s * 1.4, s, s * 0.8), (x, y, s * 0.35), ("pit_stone", "cinder", "pit_stone_lt")[k % 3],
            rot=(rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4), rng.uniform(0, 3)), bev=0.015)
    cyl("ash", 0.50, 0.03, (0.0, -0.08, 0.015), "cinder", verts=20).scale = (1.15, 0.55, 1.0)
    return centre(1.45)


def _heap(prefix, rng, count, box, size, colours=("pit_stone", "pit_stone_dk", "pit_stone_lt", "pit_stone", "cinder")):
    """Broken stone heaped in a box (x0, x1, y0, y1, z0, z1): blocks of about
    `size` tumbled every way, the biggest at the bottom."""
    x0, x1, y0, y1, z0, z1 = box
    for k in range(count):
        z = z0 + (z1 - z0) * (k / max(1, count - 1)) ** 0.8
        s = size * rng.uniform(0.7, 1.25) * (1.15 - 0.3 * (z - z0) / max(0.01, z1 - z0))
        blk("%s_%d" % (prefix, k), (s * rng.uniform(1.0, 1.6), s * rng.uniform(0.8, 1.3), s * rng.uniform(0.6, 1.0)),
            (rng.uniform(x0, x1), rng.uniform(y0, y1), z), colours[rng.randrange(len(colours))],
            rot=(rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5), rng.uniform(0, math.pi)), bev=0.03)


def _femur(name, a, b, r=0.035):
    """A long bone: the shaft and a knuckle at each end."""
    limb(name, a, b, r, r * 0.85, "bone")
    for k, p in enumerate((a, b)):
        sphere("%s_knob_%d" % (name, k), r * 1.7, p, "bone").scale = (1.0, 1.0, 0.8)


def prop_rubble_throne():
    """The Pit Lord's seat: a throne heaped up out of black rubble and
    cinders -- a step, a seat, arms of piled stone and a high back broken off
    in shards -- with bones in the heap (a skull on each arm and one at the
    crest, long bones jutting out), and ember-light glowing faintly in the
    cracks."""
    rng = random.Random(1666)
    # The step and the base, wide and low.
    _heap("step", rng, 14, (-0.70, 0.70, -1.00, -0.70, 0.04, 0.10), 0.20)
    _heap("base", rng, 26, (-1.05, 1.05, -0.70, 0.60, 0.06, 0.30), 0.28)
    # The seat: a low heap under two broad slabs laid across it, the paler
    # one in front, so there is somewhere plainly to sit. Heaped up to them,
    # the seat was lost in the rubble.
    _heap("seat", rng, 10, (-0.48, 0.48, -0.30, 0.30, 0.30, 0.42), 0.22)
    blk("seat_slab_a", (1.10, 0.48, 0.12), (0.0, -0.22, 0.58), "pit_stone_lt", rot=(0.03, -0.02, 0.03), bev=0.03)
    blk("seat_slab_b", (1.00, 0.40, 0.12), (0.02, 0.18, 0.60), "pit_stone", rot=(-0.02, 0.03, -0.04), bev=0.03)
    blk("seat_front", (1.04, 0.10, 0.34), (0.0, -0.46, 0.38), "pit_stone_dk", rot=(0.0, 0.02, 0.02), bev=0.03)
    # The back: one great slab stood on end behind the seat and leaning back
    # a little, rubble banked against its foot and sides, and shards stood up
    # along its top in a ragged crest, the tallest in the middle.
    blk("back_slab", (1.02, 0.24, 1.18), (0.0, 0.52, 1.18), "pit_stone", rot=(math.radians(-6), 0, 0.03), bev=0.04)
    blk("back_face", (0.78, 0.04, 0.86), (0.0, 0.39, 1.16), "pit_stone_dk", rot=(math.radians(-6), 0, 0.03), bev=0.02)
    _heap("back_l", rng, 9, (-0.95, -0.55, 0.30, 0.72, 0.40, 1.30), 0.24)
    _heap("back_r", rng, 9, (0.55, 0.95, 0.30, 0.72, 0.40, 1.30), 0.24)
    for k, (x, h, lean) in enumerate(((-0.62, 0.36, -0.30), (-0.34, 0.54, -0.12), (-0.08, 0.70, -0.04),
                                      (0.18, 0.64, 0.08), (0.44, 0.48, 0.18), (0.66, 0.30, 0.30))):
        blk("shard_%d" % k, (0.24, 0.20, h), (x, 0.58, 1.72 + h / 2 - 0.12), ("pit_stone", "pit_stone_lt")[k % 2],
            rot=(0.05, lean, 0.12 * (k % 3 - 1)), bev=0.03)
    # The arms: piled stone either side, a broad stone laid on top of each.
    for sx in (-1, 1):
        _heap("arm_%d" % sx, rng, 10, (sx * 0.62, sx * 0.98, -0.42, 0.36, 0.34, 0.78), 0.22)
        blk("arm_top_%d" % sx, (0.40, 0.80, 0.12), (sx * 0.80, -0.05, 0.92), "pit_stone_lt",
            rot=(0.04, sx * 0.06, sx * 0.05), bev=0.03)
    # Bones in the heap: crossed long bones and a skull over the back, a
    # skull on each arm, more in the rubble at the foot.
    _femur("bone_crest_a", (-0.40, 0.36, 1.58), (0.30, 0.34, 2.00))
    _femur("bone_crest_b", (0.40, 0.36, 1.58), (-0.28, 0.34, 1.98))
    pp._skull("skull_crest", 0.0, 0.28, 1.80, tilt=(math.radians(-8), 0, 0), r=0.15)
    for sx in (-1, 1):
        pp._skull("skull_arm_%d" % sx, sx * 0.80, -0.32, 1.07, tilt=(math.radians(-6), 0, math.radians(-sx * 18)),
                  r=0.12)
    _femur("bone_base_a", (-0.95, -0.55, 0.28), (-0.58, -0.90, 0.12))
    _femur("bone_base_b", (0.62, -0.70, 0.24), (1.02, -0.42, 0.18))
    for k in range(4):
        ring("rib_%d" % k, 0.13 - k * 0.012, 0.018, (-0.52 + k * 0.05, -0.80, 0.28 + k * 0.012), "bone",
             rot=(math.radians(70), 0, math.radians(20)))
    # Ember-light in the cracks: thin slivers sunk in the seams -- under the
    # seat's front edge, along the step, down the back slab's sides -- so a
    # line of it shows and not a lamp. Round and proud, they were polka dots.
    for k, (p, ln, a) in enumerate((((-0.20, -0.52, 0.53), 0.42, 0.04), ((0.28, -0.52, 0.55), 0.26, -0.06),
                                    ((-0.48, -0.86, 0.15), 0.22, 0.30), ((0.40, -0.88, 0.14), 0.20, -0.20),
                                    ((-0.56, 0.36, 0.80), 0.26, 1.35), ((0.56, 0.37, 0.92), 0.22, 1.45),
                                    ((-0.86, -0.46, 0.62), 0.16, 0.20), ((0.84, -0.47, 0.58), 0.16, -0.25),
                                    ((0.05, -0.74, 0.30), 0.18, 0.10))):
        blk("ember_%d" % k, (ln, 0.05, 0.035), p, "ember_dim", rot=(0, a, 0), emit=1.1, bev=0)
    return centre(3.20)


# --- Hoarfang's head ----------------------------------------------------------------
# The frost dragon's own colours (blender_creatures.py, "drake"), with the
# horns white rather than its sheet's dark blue: a trophy is the head as it
# is remembered.
bp.PALETTE.update({
    "hf_scale":  (0.630, 0.740, 0.860), "hf_scale_dk": (0.360, 0.490, 0.670),
    "hf_belly":  (0.780, 0.860, 0.940), "hf_horn":     (0.940, 0.960, 0.985),
    "hf_tooth":  (0.900, 0.970, 1.000), "hf_mouth":    (0.160, 0.200, 0.320),
    "hf_eye":    (0.550, 0.950, 1.000), "hf_rime":     (0.800, 0.950, 1.000),
    "hf_oak":    (0.250, 0.155, 0.095), "hf_oak_lt":   (0.370, 0.240, 0.150),
})
TROPHY_ELEVATION = -10.0      # from a little below, as a head mounted high on a wall is seen


def _gather(before, pivot, rot):
    """Everything built since `before`, turned about `pivot` by `rot`."""
    p = bpy.data.objects.new("head_pivot", None)
    bpy.context.collection.objects.link(p)
    p.location = pivot
    for ob in list(bpy.context.scene.objects):
        if ob in before or ob is p or ob.parent is not None or ob.type != "MESH":
            continue
        ob.parent = p
        ob.location = ob.location - Vector(pivot)
    p.rotation_euler = rot
    return p


def prop_hoarfang_trophy():
    """The head of Hoarfang, the frost dragon, mounted on a dark oak shield
    to hang on a wall: pale ice-blue scales, white horns swept back, the jaw
    a little open on icy teeth, the eyes still holding a little cold light.
    Seen from the front and a little below; the head turned a quarter to one
    side, so the length of the snout shows -- square on, it was a blob --
    but only a little: turned a quarter, the far horn was a stick and the
    mouth was lost."""
    zc = 0.62
    W, H = 0.66, 0.86
    # The plaque, its back to the wall at +Y: a shield, pointed at the foot,
    # with a paler rim and four iron studs.
    blk("plaque", (W, 0.07, H * 0.70), (0, 0.04, zc + H * 0.12), "hf_oak", bev=0.02)
    blk("plaque_point", (W * 0.62, 0.07, W * 0.62), (0, 0.04, zc - H * 0.21), "hf_oak", rot=(0, math.radians(45), 0),
        bev=0.02)
    blk("plaque_rim", (W + 0.07, 0.05, H * 0.73), (0, 0.075, zc + H * 0.12), "hf_oak_lt", bev=0.015)
    blk("plaque_rim_point", (W * 0.68, 0.05, W * 0.68), (0, 0.075, zc - H * 0.21), "hf_oak_lt",
        rot=(0, math.radians(45), 0), bev=0.015)
    for sx in (-1, 1):
        for z in (zc + H * 0.40, zc - H * 0.06):
            sphere("stud_%d_%d" % (sx, int(z * 100)), 0.028, (sx * (W / 2 - 0.07), -0.002, z), "iron", rough=0.5)
    before = set(bpy.context.scene.objects)
    # The head is built about the point where its neck meets the board, the
    # snout toward -Y, and turned once it is whole.
    P = Vector((0.0, -0.06, zc))

    def at(x, y, z):
        return tuple(P + Vector((x, y, z)))
    # A short neck, cut where it meets the board, rimed along the cut.
    sphere("neck", 0.15, at(0, -0.06, -0.03), "hf_scale").scale = (1.0, 0.9, 1.1)
    ring("neck_rime", 0.15, 0.028, at(0, 0.04, -0.03), "hf_rime", rot=(math.pi / 2, 0, 0)).scale = (1.0, 1.15, 1.0)
    # Skull and a long snout, its ridge darker; a glint of cold in the eyes.
    sphere("skull", 0.15, at(0, -0.21, 0.04), "hf_scale").scale = (1.0, 1.2, 0.85)
    sphere("snout", 0.09, at(0, -0.47, 0.00), "hf_scale").scale = (0.95, 2.6, 0.75)
    sphere("nose", 0.066, at(0, -0.70, 0.0), "hf_scale").scale = (1.0, 1.0, 0.85)
    limb("snout_ridge", at(0, -0.30, 0.07), at(0, -0.66, 0.05), 0.026, 0.018, "hf_scale_dk")
    for sx in (-1, 1):
        sphere("nostril_%d" % sx, 0.016, at(sx * 0.032, -0.75, 0.03), "hf_mouth")
        sphere("eye_%d" % sx, 0.030, at(sx * 0.095, -0.30, 0.07), "hf_eye", emit=0.8)
        limb("brow_%d" % sx, at(sx * 0.05, -0.36, 0.11), at(sx * 0.14, -0.22, 0.14), 0.028, 0.020, "hf_scale_dk")
    # The jaw dropped open at the front: dark inside, icy teeth top and bottom.
    sphere("mouth", 0.08, at(0, -0.45, -0.09), "hf_mouth").scale = (0.82, 2.6, 0.60)
    jaw = sphere("jaw", 0.08, at(0, -0.43, -0.17), "hf_belly")
    jaw.scale = (0.90, 2.25, 0.45)
    jaw.rotation_euler = (math.radians(-14), 0, 0)
    for i in range(5):
        y = -0.36 - 0.075 * i
        for sx in (-1, 1):
            cone("tooth_up_%d_%d" % (i, sx), 0.022, 0.085, at(sx * 0.062, y, -0.075), "hf_tooth",
                 rot=(math.pi, 0, 0), verts=6)
            cone("tooth_lo_%d_%d" % (i, sx), 0.019, 0.070, at(sx * 0.056, y + 0.02, -0.14 - 0.022 * i), "hf_tooth",
                 verts=6)
    # Horns, white, swept back toward the wall and up; a shorter pair from
    # the jaw's hinge; a frill of dark spines down each side of the neck.
    for sx in (-1, 1):
        pts = [at(sx * 0.09, -0.16, 0.12), at(sx * 0.22, -0.03, 0.28), at(sx * 0.33, 0.10, 0.45),
               at(sx * 0.40, 0.20, 0.62)]
        radii = (0.060, 0.046, 0.030, 0.008)
        for k in range(3):
            limb("horn_%d_%d" % (sx, k), pts[k], pts[k + 1], radii[k], radii[k + 1], "hf_horn")
        limb("horn2_%d_0" % sx, at(sx * 0.12, -0.15, -0.03), at(sx * 0.25, -0.02, -0.01), 0.034, 0.022, "hf_horn")
        limb("horn2_%d_1" % sx, at(sx * 0.25, -0.02, -0.01), at(sx * 0.34, 0.08, 0.05), 0.022, 0.006, "hf_horn")
        for k in range(3):
            limb("frill_%d_%d" % (sx, k), at(sx * 0.12, -0.04 + k * 0.03, -0.06 - k * 0.05),
                 at(sx * 0.24, 0.02 + k * 0.03, -0.12 - k * 0.06), 0.022, 0.004, "hf_scale_dk")
    # Turned toward the viewer's left, so the snout's length and the open
    # jaw show and both horns stand clear of the skull.
    _gather(before, tuple(P), (0, 0, math.radians(-38)))
    return centre(1.40, TROPHY_ELEVATION), TROPHY_ELEVATION


# --- the lizardmen's cave -----------------------------------------------------------
bp.PALETTE.update({
    "lc_mud":    (0.330, 0.270, 0.200), "lc_mud_dk": (0.230, 0.190, 0.145),
    "lc_mud_lt": (0.450, 0.410, 0.280), "lc_cord":   (0.560, 0.450, 0.300),
    "lc_feather_red": (0.700, 0.200, 0.160), "lc_feather_teal": (0.180, 0.520, 0.500),
    "lc_bead":   (0.880, 0.760, 0.300),
})


def _reed_clump(prefix, rng, cx, cy, radius, count, h=(0.45, 0.90)):
    """Reeds and bulrushes standing in a ring of mud: thin blades, every
    third with its brown cigar head."""
    for k in range(count):
        a = rng.uniform(0, math.tau)
        r_ = rng.uniform(0.0, radius)
        hh = rng.uniform(*h)
        x, y = cx + math.cos(a) * r_, cy + math.sin(a) * r_ * 0.7
        lean = rng.uniform(-0.20, 0.20)
        cyl("%s_blade_%d" % (prefix, k), 0.032, hh, (x + lean * hh * 0.5, y, hh / 2), ("reed", "reed_dk")[k % 2],
            rot=(0, lean, 0), verts=6)
        if k % 3 == 0:
            cyl("%s_head_%d" % (prefix, k), 0.062, 0.20, (x + lean * hh, y, hh + 0.02), "cattail", rot=(0, lean, 0),
                verts=8)


def _lizard_skull(prefix, x, y, z, s=1.0):
    """A lizard's skull hung flat against a face, snout down: a flat, broad
    back, two great eye holes, and a long narrowing snout with a row of
    teeth down each side. No horns: with them it was a cow's."""
    sphere(prefix + "_back", 0.17 * s, (x, y, z), "bone_white").scale = (1.2, 0.42, 0.95)
    sphere(prefix + "_snout", 0.085 * s, (x, y - 0.01, z - 0.30 * s), "bone_white").scale = (0.95, 0.42, 2.3)
    sphere(prefix + "_tip", 0.062 * s, (x, y - 0.015, z - 0.49 * s), "bone_white").scale = (1.0, 0.5, 0.9)
    limb(prefix + "_ridge", (x, y - 0.045 * s, z - 0.16 * s), (x, y - 0.04 * s, z - 0.46 * s), 0.020 * s, 0.015 * s,
         "bone")
    for sx in (-1, 1):
        sphere("%s_eye_%d" % (prefix, sx), 0.056 * s, (x + sx * 0.080 * s, y - 0.05 * s, z + 0.01 * s), "void")
        sphere("%s_nare_%d" % (prefix, sx), 0.018 * s, (x + sx * 0.030 * s, y - 0.045 * s, z - 0.51 * s), "void")
        for i in range(4):
            cone("%s_tooth_%d_%d" % (prefix, sx, i), 0.020 * s, 0.07 * s,
                 (x + sx * (0.078 - 0.008 * i) * s, y - 0.03 * s, z - (0.22 + 0.08 * i) * s), "bone",
                 rot=(0, sx * math.radians(90), 0), verts=5)


def prop_lizard_cave():
    """The lizardmen's cave in the swamp: a low mound of mud banked up and
    grown over with reeds, a dark doorway in it framed in bones, a lizard's
    skull hung over it, a charm of bone and feathers hanging by it.

    The doorway is bear_den's to the number -- the mouth, the jambs and the
    lintel, the bank they are cut in, and the bones lying at the door, which
    are the lowest thing in both pictures and so set where make_props.ps1
    sits them -- so the game can give this the den's collision as it is."""
    rng = random.Random(4419)
    # bear_den's mound, mouth, lintel and jambs: the same sizes and places.
    blk("bank", (3.4, 1.8, 1.10), (0, 0.55, 0.55), "lc_mud", bev=0.30)
    blk("bank_top", (2.9, 1.5, 0.40), (0, 0.60, 1.20), "lc_mud_lt", bev=0.18)
    blk("mouth", (1.30, 0.30, 0.92), (0, -0.30, 0.46), "void", bev=0.10, rough=1.0)
    blk("lintel", (2.0, 0.60, 0.34), (0, -0.20, 1.06), "lc_mud_dk", rot=(0, math.radians(4), 0), bev=0.08)
    blk("jamb_l", (0.44, 0.56, 1.00), (-0.86, -0.22, 0.50), "lc_mud_dk", rot=(0, math.radians(-6), 0), bev=0.08)
    blk("jamb_r", (0.48, 0.56, 0.96), (0.88, -0.22, 0.48), "lc_mud", rot=(0, math.radians(7), 0), bev=0.08)
    # Mud slapped over the mound in lumps, and moss in the wet of it.
    for k in range(10):
        x = rng.uniform(-1.5, 1.5)
        sphere("lump_%d" % k, rng.uniform(0.18, 0.30), (x, rng.uniform(0.2, 1.0), rng.uniform(0.9, 1.25)),
               ("lc_mud", "lc_mud_lt", "swamp_moss")[k % 3]).scale = (1.4, 1.0, 0.55)
    # The bones of the doorway: long bones stood up the front of each jamb,
    # clear of the opening, and a spine laid along the lintel's face.
    for sx, x0 in ((-1, -0.86), (1, 0.88)):
        for j, dx in enumerate((-0.10, 0.10)):
            _femur("jamb_bone_%d_%d" % (sx, j), (x0 + dx, -0.52, 0.08), (x0 + dx * 0.6, -0.52, 0.86), r=0.040)
    limb("spine", (-0.92, -0.53, 1.08), (0.92, -0.53, 1.13), 0.035, 0.030, "bone")
    for k in range(9):
        sphere("vertebra_%d" % k, 0.050, (-0.80 + k * 0.20, -0.54, 1.085 + 0.005 * k), "bone_white").scale = (0.8, 0.8, 1.1)
    # The skull over the door, hung on the mound's face above the lintel.
    # In front of the lintel, and high enough that its snout stops short of
    # the opening.
    _lizard_skull("skull_door", 0.0, -0.56, 1.62, s=1.15)
    # Reeds: a stand at each side where the den has its trees -- so the mound
    # spans the picture as the den's does -- and tufts along its top.
    for sx in (-1, 1):
        _reed_clump("reeds_%d" % sx, rng, sx * 1.80, 0.0, 0.22, 16)
    for k, (x, y) in enumerate(((-1.25, 0.55), (-0.55, 0.95), (0.40, 0.90), (1.15, 0.60))):
        _reed_clump("tuft_%d" % k, rng, x, y, 0.16, 8, h=(0.30, 0.55))
        for ob in [o for o in bpy.context.scene.objects if o.name.startswith("tuft_%d_" % k)]:
            ob.location.z += 1.30
    # The charm: a cord from the lintel's end with a little bone, beads and
    # a fan of red and teal feathers, hanging in front of the right jamb.
    cx, cy = 0.98, -0.60
    limb("charm_cord", (cx, cy, 1.10), (cx, cy, 0.66), 0.012, 0.012, "lc_cord")
    for k, z in enumerate((0.92, 0.86, 0.80)):
        sphere("charm_bead_%d" % k, 0.030, (cx, cy, z), ("lc_bead", "lc_feather_teal", "lc_bead")[k])
    _femur("charm_bone", (cx - 0.07, cy - 0.01, 0.72), (cx + 0.07, cy - 0.01, 0.72), r=0.020)
    for k, (a, colour) in enumerate(((-28, "lc_feather_red"), (0, "lc_feather_teal"), (28, "lc_feather_red"))):
        blk("charm_feather_%d" % k, (0.05, 0.015, 0.20), (cx + math.sin(math.radians(a)) * 0.07, cy - 0.02,
                                                          0.58 - math.cos(math.radians(a)) * 0.04), colour,
            rot=(0, math.radians(a), 0), bev=0)
    # bear_den's bones at the door, where its are.
    for i, (x, y) in enumerate(((-0.5, -0.80), (0.35, -0.95), (0.0, -0.70))):
        cyl("bone_%d" % i, 0.035, 0.36, (x, y, 0.05), "bone", rot=(math.radians(90), 0, math.radians(30 + i * 50)),
            verts=8)
    sphere("skull", 0.11, (0.62, -0.78, 0.10), "bone")
    return 4.0


# --- the cauldron gone cold ---------------------------------------------------------
bp.PALETTE.update({
    "pot_inside": (0.070, 0.066, 0.072), "ash_grey": (0.560, 0.545, 0.520), "ash_grey_dk": (0.400, 0.390, 0.375),
})


def prop_cauldron_cold():
    """The brewing cauldron (blender_props.prop_cauldron, the very same pot,
    legs and handles) gone cold and empty: no fire under it and no brew in
    it, the inside dark, standing in a ring of grey ash with the sticks of
    the old fire burnt to charcoal. The front stick lies where the hot one's
    front log does, so the two pictures sit on the floor by the same row."""
    span = bp.prop_cauldron()
    for ob in list(bpy.context.scene.objects):
        if ob.name.startswith(("brew", "bubble", "flame")):
            bpy.data.objects.remove(ob, do_unlink=True)
    # Empty: the dark inside of the pot where the brew stood, a crust of
    # old brew dried round it.
    cyl("inside", 0.268, 0.012, (0, 0, 0.962), "pot_inside", verts=28, rough=0.9)
    ring("crust", 0.245, 0.012, (0, 0, 0.966), "ash_grey_dk", rough=0.9)
    # The fire's sticks burnt to charcoal: the same four, reaching out past
    # the ash as the logs did past the fire, one burnt short, with grey ends.
    for k in range(4):
        ob = bpy.data.objects.get("log_%d" % k)
        if ob is None:
            continue
        ob.data.materials[0] = material("log_cold_%d" % k, "char_lt", 0.95)
        if k == 2:
            ob.scale = (ob.scale[0] * 0.70, ob.scale[1], ob.scale[2])
        a = k / 4 * math.tau
        end = 0.20 + (0.14 if k == 2 else 0.15)            # short of the front stick's end
        sphere("stick_ash_%d" % k, 0.045, (math.cos(a) * end, math.sin(a) * end, 0.05), "ash_grey").scale = (1.0, 1.0, 0.7)
    # The ash: a flat ring of it under the pot, inside the old fire's reach.
    cyl("ash", 0.31, 0.016, (0, 0, 0.008), "ash_grey", verts=24, rough=1.0)
    cyl("ash_inner", 0.20, 0.018, (0.02, 0.01, 0.010), "ash_grey_dk", verts=20, rough=1.0)
    rng = random.Random(23)
    for k in range(7):
        a = rng.uniform(0, math.tau)
        r_ = rng.uniform(0.10, 0.27)
        sphere("ash_lump_%d" % k, rng.uniform(0.03, 0.05), (math.cos(a) * r_, math.sin(a) * r_ * 0.9 + 0.02, 0.015),
               ("ash_grey", "ash_grey_dk", "char")[k % 3]).scale = (1.3, 1.0, 0.5)
    return span


PROPS = {
    # Act II.
    "thread_net":            (prop_thread_net, CHAR_PX),
    "pit_rune":              (prop_pit_rune, 48),
    "rubble_throne":         (prop_rubble_throne, 96),
    "hoarfang_trophy":       (prop_hoarfang_trophy, 64),
    "lizard_cave":           (prop_lizard_cave, 144),
    "cauldron_cold":         (prop_cauldron_cold, 48),
    "thread_rack":           (prop_thread_rack, RACK_PX),
    "thread_strand_crimson": (prop_thread_strand_crimson, RACK_PX),
    "thread_strand_ivory":   (prop_thread_strand_ivory, RACK_PX),
    "thread_strand_gold":    (prop_thread_strand_gold, RACK_PX),
    "thread_strand_cobalt":  (prop_thread_strand_cobalt, RACK_PX),
    "thread_strand_moss":    (prop_thread_strand_moss, RACK_PX),
    "thread_strand_violet":  (prop_thread_strand_violet, RACK_PX),
    "thread_snarl":          (prop_thread_snarl, RACK_PX),
    "dress_form_bare":       (prop_dress_form_bare, 64),
    "dress_form_bodice":     (prop_dress_form_bodice, 64),
    "dress_form_skirt":      (prop_dress_form_skirt, 64),
    "dress_form_full":       (prop_dress_form_full, 64),
    "portrait_gown":         (prop_portrait_gown, 60),
    "nail_patch":            (prop_nail_patch, 60),
    "sewing_desk":           (prop_sewing_desk, DESK_PX),
    "shears_lying":          (prop_shears_lying, SHEARS_PX),
    "giant_spool":           (prop_giant_spool, 64),
    "clothier_sign":         (prop_clothier_sign, 40),
    "door_fallen":           (prop_door_fallen, 48),
    "forge_chimney":    (prop_forge_chimney, 32),
    "nightmare_hold":   (prop_nightmare_hold, 64),
    "mini_anchor":      (prop_mini_anchor, 56),
    "square_anchor":    (prop_square_anchor, SQUARE_ANCHOR_PX),
    "dream_barrier":    (prop_dream_barrier, 64),
    "dream_barrier_v":  (prop_dream_barrier_v, 64),
    "dawn_bells":       (prop_dawn_bells, 80),
    "dawn_bell_great":  (prop_dawn_bell_great, 96),
    "dawn_chime":       (prop_dawn_chime, 40),
    "black_chains_chair":       (prop_black_chains_chair, 80),
    "black_chains_chair_empty": (prop_black_chains_chair_empty, 80),
    "giant_anvil":      (prop_giant_anvil, 112),
    "hanging_hammer":   (prop_hanging_hammer, 96),
    "great_anvil_bound": (prop_great_anvil_bound, 80),
    "drying_frame_tall": (prop_drying_frame_tall, 128),
    "tannery_post_bound": (prop_tannery_post_bound, 64),
    "steaming_vat":     (prop_steaming_vat, 40),
    "wolf_pelt_rack":   (prop_wolf_pelt_rack, 48),
    "frost_patch":      (prop_frost_patch, 48),
    "cask_stack_tall":  (prop_cask_stack_tall, 128),
    "web_patch":        (prop_web_patch, 48),
    "web_strand":       (prop_web_strand, 56),
    "apron_hook":       (prop_apron_hook, 28),
    "mug_half":         (prop_mug_half, 12),
    "loaf_rack":        (prop_loaf_rack, 28),
    "candlestick":      (prop_candlestick, 28),
    "sampler":          (prop_sampler, 32),
    "cellar_lock":      (prop_cellar_lock, 56),
    "claw_marks":       (prop_claw_marks, 24),
    "knight_scrap":     (prop_knight_scrap, 36),
    "boo_note":         (prop_boo_note, 14),
    "desk_tilted":      (prop_desk_tilted, 56),
    "papers_drift":     (prop_papers_drift, 40),
    "great_desk":       (prop_great_desk, 88),
}
