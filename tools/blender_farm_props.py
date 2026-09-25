# =============================================================================
#  blender_farm_props.py - Farmer Aldous's bees, in the Westwold: a painted box
#  hive in three colours, a straw skep on a stump, the open-fronted bee shed
#  with skeps on its shelves, and a bed of lavender for them to work.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only beehive,bee_skep
#
#  Built with blender_props.py's own tools and registered into its PROPS
#  table, the way blender_frostreach_props.py is: that file hands itself over.
#
#  One Blender unit is about thirty-three pixels here, the same as the barrel
#  and the crates the apiary stands among. The bees are not modelled: they are
#  a few pixels each and move, so the game draws them round a hive in code
#  (World::DrawBees).
#
#  Two things decide whether these read at forty pixels. A box hive is a
#  little house -- pale painted boxes under a dark gabled roof with a stone on
#  it -- and the shape of that roof is most of what says "hive" rather than
#  "crate". A skep is straw, and straw laid in clean rings reads as a striped
#  dome, the way the first thatch here read as planks: so its coils are broken
#  into hundreds of short straws of uneven length, lean and tone.
# =============================================================================

import math
import random

import bpy
from mathutils import Vector

import blender_props as bp

blk, cyl, cone, sphere = bp.blk, bp.cyl, bp.cone, bp.sphere

bp.PALETTE.update({
    # Hive paint: soft colours, as a beekeeper paints them, so the bees know
    # their own door. The darker of each is the flared foot of every lift.
    "fm_paint_cream":    (0.930, 0.890, 0.780),
    "fm_paint_cream_dk": (0.760, 0.700, 0.580),
    "fm_paint_blue":     (0.560, 0.720, 0.860),
    "fm_paint_blue_dk":  (0.380, 0.520, 0.680),
    "fm_paint_sage":     (0.640, 0.760, 0.540),
    "fm_paint_sage_dk":  (0.440, 0.560, 0.380),
    "fm_roof":           (0.560, 0.360, 0.270),
    "fm_roof_dk":        (0.380, 0.230, 0.170),
    "fm_wood":           (0.560, 0.400, 0.250),
    "fm_wood_dk":        (0.360, 0.250, 0.160),
    "fm_wood_lt":        (0.700, 0.540, 0.350),
    "fm_slot":           (0.090, 0.060, 0.050),
    "fm_stone":          (0.560, 0.550, 0.530),
    # Straw for the skeps, in three tones, and the bramble that binds its coils.
    "fm_straw":          (0.840, 0.700, 0.380),
    "fm_straw_lt":       (0.950, 0.840, 0.540),
    "fm_straw_dk":       (0.620, 0.470, 0.220),
    "fm_straw_bind":     (0.420, 0.300, 0.160),
    # Lavender.
    "fm_lav":            (0.600, 0.450, 0.830),
    "fm_lav_dk":         (0.420, 0.300, 0.650),
    "fm_lav_lt":         (0.760, 0.640, 0.940),
    "fm_lav_leaf":       (0.520, 0.620, 0.520),
    "fm_lav_leaf_dk":    (0.360, 0.460, 0.380),
    "fm_soil":           (0.330, 0.240, 0.170),
    "fm_soil_dk":        (0.240, 0.170, 0.120),
})


def prism(name, outline, depth, y, colour, bev=0.01):
    """A solid from a polygon in the X-Z plane (the face the camera sees),
    pushed back `depth` along Y and centred on `y`: a gable end, a wedge."""
    import bmesh
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    front = [bm.verts.new((x, y - depth / 2, z)) for x, z in outline]
    back = [bm.verts.new((x, y + depth / 2, z)) for x, z in outline]
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    n = len(outline)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((front[i], front[j], back[j], back[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    ob.data.materials.append(bp.material(name, colour))
    if bev > 0:
        bp.bevel(ob, bev)
    return ob


def rod(name, a, b, radius, colour, verts=6):
    """A thin cylinder from point a to point b: one straw, one stem."""
    a, b = Vector(a), Vector(b)
    d = b - a
    ob = cyl(name, radius, d.length, tuple((a + b) / 2), colour, verts=verts)
    ob.rotation_euler = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_euler()
    return ob


# =================================================================================
#  The box hive
# =================================================================================
def _box_hive(body, body_dk):
    """A W.B.C. hive: stubby legs, a floor with a landing board out to the
    front, the brood box with its slot of a door, two lifts that flare at the
    foot, and a gabled roof with a stone on the ridge against the wind."""
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.09, 0.09, 0.16), (sx * 0.24, sy * 0.24, 0.08), "fm_wood_dk", bev=0.01)
    blk("stand", (0.68, 0.68, 0.06), (0, 0, 0.19), "fm_wood", bev=0.015)
    blk("landing", (0.40, 0.22, 0.035), (0, -0.40, 0.205), "fm_wood_lt", rot=(math.radians(-14), 0, 0), bev=0.008)
    z = 0.22
    blk("brood", (0.58, 0.58, 0.24), (0, 0, z + 0.12), body, bev=0.02)
    # The door: a long dark slot at the foot of the front, with the entrance
    # block's lip over it.
    blk("slot", (0.30, 0.04, 0.05), (0, -0.29, z + 0.035), "fm_slot", bev=0)
    blk("slot_lip", (0.36, 0.04, 0.03), (0, -0.30, z + 0.075), body_dk, bev=0.005)
    z += 0.24
    for k in range(2):
        blk("lift_foot_%d" % k, (0.66, 0.66, 0.05), (0, 0, z + 0.025), body_dk, bev=0.012)
        blk("lift_%d" % k, (0.62, 0.62, 0.17), (0, 0, z + 0.05 + 0.085), body, bev=0.02)
        z += 0.22
    # The roof: a skirt round its foot, a gable facing front, and two warm
    # dark slopes over it with an overhang -- the lit one and the shaded one
    # are what make it a roof and not a lid.
    blk("skirt", (0.72, 0.72, 0.08), (0, 0, z + 0.04), body, bev=0.012)
    z += 0.08
    half, rise = 0.36, 0.26
    prism("gable", [(-half, z), (half, z), (0.0, z + rise)], 0.70, 0.0, body)
    pitch = math.atan2(rise, half)
    run = math.hypot(half, rise) + 0.10
    for side in (-1, 1):
        # Along each slope of the gable, standing a hair proud of it, and
        # running on past the eave rather than past the ridge.
        cx = side * (half / 2 + 0.05 * math.cos(pitch) + 0.028 * math.sin(pitch))
        cz = z + rise / 2 - 0.05 * math.sin(pitch) + 0.028 * math.cos(pitch)
        blk("roof_%d" % side, (run, 0.84, 0.05), (cx, 0, cz), "fm_roof", rot=(0, side * pitch, 0), bev=0.012)
    blk("ridge", (0.07, 0.86, 0.05), (0, 0, z + rise + 0.035), "fm_roof_dk", bev=0.012)
    sphere("roof_stone", 0.075, (0.05, -0.10, z + rise + 0.10), "fm_stone")
    return 1.75


def prop_beehive():
    return _box_hive("fm_paint_cream", "fm_paint_cream_dk")


def prop_beehive_blue():
    return _box_hive("fm_paint_blue", "fm_paint_blue_dk")


def prop_beehive_green():
    return _box_hive("fm_paint_sage", "fm_paint_sage_dk")


# =================================================================================
#  The skep
# =================================================================================
def skep(prefix, x, y, z0, R, Hd, rng, rows=9):
    """A coiled straw skep standing on z0: a dome of mid straw, then hundreds
    of short straws laid round it in broken coils -- uneven lengths, each
    leaning its own way, three tones -- with the bramble binding showing here
    and there. The rings are only suggested, never drawn."""
    # The body, a stack of coils following the dome, a little inside the
    # straws so none of it shows as a smooth band.
    for k in range(rows):
        t0 = k / rows
        zc = z0 + (t0 + 0.5 / rows) * Hd
        r = R * math.sqrt(max(0.0, 1.0 - ((zc - z0) / (Hd * 1.04)) ** 2)) * 0.94
        cyl("%s_core_%d" % (prefix, k), max(r, 0.02), Hd / rows * 1.08, (x, y, zc), "fm_straw", verts=20)
    tones = ("fm_straw", "fm_straw", "fm_straw_lt", "fm_straw_lt", "fm_straw_dk")
    n = 0
    for k in range(rows):
        zc = z0 + (k + 0.5) / rows * Hd
        r = R * math.sqrt(max(0.0, 1.0 - ((zc - z0) / (Hd * 1.04)) ** 2))
        if r < 0.03:
            continue
        a = rng.random() * math.tau
        while a < math.tau + 0.2:
            length = (0.05 + rng.random() * 0.09) * (R / 0.28)
            step = length / max(r, 0.05)
            a0, a1 = a, a + step
            lean = rng.uniform(-0.35, 0.35)
            dz = rng.uniform(-0.012, 0.012) * (Hd / 0.38)
            # Radial out a hair, so the straws sit proud of the body.
            p0 = (x + math.cos(a0) * r * 1.03, y + math.sin(a0) * r * 1.03, zc + dz - lean * length * 0.25)
            p1 = (x + math.cos(a1) * r * 1.03, y + math.sin(a1) * r * 1.03, zc + dz + lean * length * 0.25)
            tone = tones[rng.randrange(len(tones))]
            rod("%s_straw_%d" % (prefix, n), p0, p1, 0.013 * (R / 0.28) + rng.random() * 0.004, tone)
            n += 1
            a = a1 - step * rng.uniform(0.05, 0.3)       # overlapping, never butted end to end
        # Bramble binding, in pieces: where the coil's wrap shows through.
        for j in range(rng.randrange(2, 5)):
            ab = rng.random() * math.tau
            p0 = (x + math.cos(ab) * r * 1.05, y + math.sin(ab) * r * 1.05, zc - Hd / rows * 0.4)
            p1 = (x + math.cos(ab + 0.05) * r * 1.05, y + math.sin(ab + 0.05) * r * 1.05, zc + Hd / rows * 0.4)
            rod("%s_bind_%d_%d" % (prefix, k, j), p0, p1, 0.010 * (R / 0.28), "fm_straw_bind")
    # The knot at the crown, and the little arched door at the foot.
    sphere("%s_crown" % prefix, R * 0.16, (x, y, z0 + Hd * 1.0), "fm_straw_dk")
    blk("%s_door" % prefix, (R * 0.40, 0.05, R * 0.22), (x, y - R * 0.97, z0 + R * 0.11), "fm_slot", bev=0.005)
    return n


def prop_bee_skep():
    """A straw skep on a sawn stump with a board on it: the old way of keeping
    bees, and the one that looks like nothing else."""
    rng = random.Random(4417)
    cyl("stump", 0.22, 0.26, (0, 0, 0.13), "log", verts=16)
    cyl("stump_top", 0.205, 0.02, (0, 0, 0.265), "log_end", verts=16)
    for k in range(5):
        a = k / 5 * math.tau + 0.4
        rod("bark_%d" % k, (math.cos(a) * 0.225, math.sin(a) * 0.225, 0.02),
            (math.cos(a) * 0.225, math.sin(a) * 0.225, 0.24), 0.02, "log_dk")
    blk("board", (0.62, 0.56, 0.045), (0, 0, 0.29), "fm_wood", bev=0.012)
    skep("skep", 0, 0.0, 0.31, 0.28, 0.38, rng)
    return 1.1


# =================================================================================
#  The bee shed
# =================================================================================
def prop_bee_shed():
    """An open-fronted bee shed: a back and two sides of boards under a
    thatched lean-to roof, two shelves across the front and three skeps on
    each. What an apiary keeps out of the rain, and what says "bees" from the
    far side of a field."""
    rng = random.Random(8812)
    W, D = 2.30, 0.86
    front, back = -D / 2, D / 2
    blk("floor", (W + 0.10, D + 0.06, 0.08), (0, 0, 0.04), "fm_wood_dk", bev=0.012)
    # The back: upright boards of two tones, so it reads as boarding and not a panel.
    boards = 9
    for i in range(boards):
        x = -W / 2 + (i + 0.5) * W / boards
        tone = ("fm_wood", "fm_wood_dk", "fm_wood_lt")[rng.randrange(3)] if i % 2 else "fm_wood"
        blk("back_%d" % i, (W / boards - 0.012, 0.05, 1.42), (x, back - 0.03, 0.08 + 0.71), tone, bev=0.006)
    for side in (-1, 1):
        for j in range(3):
            y = front + 0.12 + j * (D - 0.18) / 3 + 0.12
            blk("side_%d_%d" % (side, j), (0.05, (D - 0.18) / 3 - 0.01, 1.42 + (0.10 - j * 0.05)),
                (side * (W / 2 - 0.02), y, 0.08 + 0.71), ("fm_wood", "fm_wood_dk", "fm_wood")[j], bev=0.006)
        blk("post_%d" % side, (0.11, 0.11, 1.62), (side * (W / 2 - 0.02), front + 0.05, 0.81), "log_dk", bev=0.015)
    # Two shelves, and three skeps on each.
    for s, z in enumerate((0.36, 0.92)):
        blk("shelf_%d" % s, (W - 0.06, D - 0.14, 0.05), (0, 0.02, z), "fm_wood_lt", bev=0.01)
        blk("shelf_lip_%d" % s, (W - 0.06, 0.05, 0.08), (0, front + 0.10, z - 0.02), "fm_wood", bev=0.008)
        for k in range(3):
            x = (k - 1) * 0.70 + rng.uniform(-0.04, 0.04)
            skep("skep_%d_%d" % (s, k), x, -0.04, z + 0.025, 0.22, 0.30, rng, rows=7)
    # The roof: a lean-to, high at the front and low at the back, thatched.
    # Laid in courses across the slope it read as a heap of planks; thatch is
    # straw combed down the slope, so it is streaks from ridge to eave in
    # three tones, of uneven length, over a slab of the middle one, with a
    # rolled eave and a ragged fringe of ends.
    eave_f, eave_b = 1.66, 1.46
    depth = D + 0.52
    pitch = math.atan2(eave_f - eave_b, depth)
    slab = depth / math.cos(pitch)
    zmid = (eave_f + eave_b) / 2 + 0.06
    blk("thatch", (W + 0.46, slab, 0.16), (0, 0.02, zmid), "thatch", rot=(-pitch, 0, 0), bev=0.05)
    top = lambda yy: zmid + 0.08 - (yy - 0.02) * math.tan(pitch)      # the slab's upper face
    tones = ("thatch", "thatch_dk", "thatch_lt", "thatch", "thatch_dk")
    n = 0
    x = -W / 2 - 0.22
    while x < W / 2 + 0.22:
        # A few straws at this x, each its own length, from somewhere near the
        # ridge down to somewhere near the eave.
        for k in range(2):
            y0 = depth / 2 - rng.uniform(0.0, 0.35)
            y1 = -depth / 2 + 0.04 + rng.uniform(0.0, 0.30)
            xx = x + rng.uniform(-0.02, 0.02)
            rod("straw_%d" % n, (xx, y0, top(y0) + 0.012), (xx + rng.uniform(-0.03, 0.03), y1, top(y1) + 0.012),
                0.024, tones[rng.randrange(len(tones))])
            n += 1
        x += 0.045 + rng.random() * 0.03
    cyl("eave_roll", 0.10, W + 0.50, (0, -depth / 2 + 0.02, eave_f + 0.02), "thatch_dk",
        rot=(0, math.radians(90), 0), verts=14)
    for i in range(26):
        x = -W / 2 - 0.20 + i * (W + 0.40) / 25
        blk("fringe_%d" % i, (0.07, 0.08, 0.06 + rng.random() * 0.07),
            (x, -depth / 2 - 0.02, eave_f - 0.08), ("thatch_dk", "thatch", "thatch_lt")[rng.randrange(3)], bev=0.02)
    return 3.0


# =================================================================================
#  The lavender bed
# =================================================================================
def prop_lavender_bed():
    """A bed of lavender edged with boards: grey-green mounds with purple
    spikes fountaining out of them, which is what bees are put beside."""
    rng = random.Random(3309)
    L, Wd = 1.64, 0.64
    blk("soil", (L - 0.06, Wd - 0.06, 0.08), (0, 0, 0.05), "fm_soil", bev=0.01)
    for side in (-1, 1):
        blk("edge_long_%d" % side, (L + 0.06, 0.07, 0.13), (0, side * Wd / 2, 0.065), "fm_wood_dk", bev=0.012)
        blk("edge_end_%d" % side, (0.07, Wd, 0.13), (side * L / 2, 0, 0.065), "fm_wood", bev=0.012)
        blk("stake_%d" % side, (0.06, 0.06, 0.18), (side * (L / 2 - 0.02), -Wd / 2 - 0.01, 0.09), "fm_wood_dk", bev=0.01)
    tones = ("fm_lav", "fm_lav", "fm_lav_dk", "fm_lav_lt")
    clumps = [(-0.62, 0.08), (-0.31, -0.10), (0.0, 0.10), (0.31, -0.08), (0.62, 0.08)]
    n = 0
    for c, (cx, cy) in enumerate(clumps):
        # A low grey-green cushion, mostly hidden under what grows out of it.
        for k in range(3):
            a = k / 3 * math.tau + rng.random()
            sphere("mound_%d_%d" % (c, k), 0.075 + rng.random() * 0.02,
                   (cx + math.cos(a) * 0.05, cy + math.sin(a) * 0.04, 0.11), ("fm_lav_leaf", "fm_lav_leaf_dk")[k % 2])
        for s_ in range(26):
            a = rng.random() * math.tau
            spread = 0.04 + rng.random() * 0.12
            base = (cx + math.cos(a) * spread * 0.4, cy + math.sin(a) * spread * 0.3, 0.12)
            h = 0.26 + rng.random() * 0.16
            tip = (base[0] + math.cos(a) * spread * 1.1, base[1] + math.sin(a) * spread * 0.7, base[2] + h)
            rod("stem_%d" % n, base, tip, 0.009, "fm_lav_leaf_dk", verts=5)
            # The flower head: a spike down the top third of the stem.
            head = sphere("head_%d" % n, 1.0, tip, tones[rng.randrange(len(tones))])
            head.scale = (0.032, 0.032, 0.085)
            n += 1
    return (1.9, 40.0)


# =================================================================================
#  Registration: name -> (builder, final pixels across)
# =================================================================================
PROPS = {
    "beehive":       (prop_beehive, 56),
    "beehive_blue":  (prop_beehive_blue, 56),
    "beehive_green": (prop_beehive_green, 56),
    "bee_skep":      (prop_bee_skep, 36),
    "bee_shed":      (prop_bee_shed, 96),
    "lavender_bed":  (prop_lavender_bed, 64),
}
