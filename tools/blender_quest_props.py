# =============================================================================
#  blender_quest_props.py - the questlines' own things (README, "Questlines"):
#  the Guild's survey cairn, which the Charter's commissions are set by; the
#  Spirewatch's cairn among the Draugr Barrows, with the eleven names cut in
#  its stone; and Old Harl's traps along his line on the Rimefall Glacier.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only guild_cairn,spirewatch_cairn,harl_trap
#
#  Built with blender_props.py's own tools and registered into its PROPS
#  table, the way blender_hexmire_props.py is.
# =============================================================================

import math
import random

import blender_props as bp
import blender_hexmire_props as hx

blk, cyl, cone, sphere = bp.blk, bp.cyl, bp.cone, bp.sphere
frustum, rod, blob = hx.frustum, hx.rod, hx.blob

bp.PALETTE.update({
    "qp_stone":    (0.560, 0.550, 0.520),
    "qp_stone_lt": (0.700, 0.690, 0.650),
    "qp_stone_dk": (0.400, 0.390, 0.370),
    "qp_pole":     (0.420, 0.300, 0.200),
    # The Guild's red and brass, as on its hall's banners.
    "qp_red":      (0.700, 0.160, 0.140),
    "qp_red_dk":   (0.480, 0.100, 0.090),
    "qp_brass":    (0.800, 0.620, 0.300),
    "qp_snow":     (0.920, 0.940, 0.980),
    "qp_rune":     (0.250, 0.280, 0.340),
    "qp_iron":     (0.300, 0.310, 0.340),
    "qp_iron_lt":  (0.520, 0.530, 0.570),
    "qp_tag":      (0.780, 0.660, 0.440),
    "qp_stake":    (0.380, 0.270, 0.180),
})


def prop_guild_cairn():
    """The Guild's survey mark: flat stones piled to a point, and a pole
    through the top with the Guild's red pennant and a brass band -- what a
    surveyor leaves at the edge of what has been charted."""
    rng = random.Random(11)
    stones = [(0.44, 0.20, 0.00, "qp_stone_dk"), (0.36, 0.18, 0.17, "qp_stone"),
              (0.28, 0.15, 0.32, "qp_stone_lt"), (0.20, 0.12, 0.45, "qp_stone")]
    for k, (r, h, z, colour) in enumerate(stones):
        blob("stone_%d" % k, r, (rng.uniform(-0.04, 0.04), rng.uniform(-0.04, 0.04), z + h * 0.5), colour,
             (1.0, 0.86, h / r))
    for k in range(6):
        a = k / 6 * math.tau + 0.3
        blob("foot_%d" % k, rng.uniform(0.09, 0.13), (math.cos(a) * 0.44, math.sin(a) * 0.38, 0.06), "qp_stone_dk",
             (1, 1, 0.7))
    rod("pole", (0, 0, 0.40), (0, 0, 1.62), 0.045, "qp_pole")
    blk("band", (0.12, 0.12, 0.06), (0, 0, 1.30), "qp_brass", bev=0, metal=0.6)
    sphere("finial", 0.07, (0, 0, 1.66), "qp_brass")
    # The pennant: chunky, or at fifty pixels it is a thread.
    blk("pennant", (0.50, 0.04, 0.26), (0.28, -0.01, 1.46), "qp_red", bev=0)
    blk("pennant_tip", (0.20, 0.04, 0.14), (0.60, -0.01, 1.48), "qp_red_dk", bev=0)
    blk("pennant_mark", (0.10, 0.05, 0.10), (0.24, -0.04, 1.46), "qp_brass", bev=0)
    return 2.0


def prop_spirewatch_cairn():
    """The Spirewatch's cairn on the heath: a long mound of stones under the
    snow, and at its head an upright slab with eleven short lines cut down its
    face -- a name to each. A broken pole lies along the top where a watch
    banner stood."""
    rng = random.Random(23)
    for k in range(18):
        x = rng.uniform(-0.95, 0.95)
        y = rng.uniform(-0.15, 0.40)
        r = rng.uniform(0.13, 0.22)
        blob("mound_%d" % k, r, (x, y, 0.08 + (0.95 - abs(x)) * 0.10), ("qp_stone", "qp_stone_dk", "qp_stone_lt")[k % 3],
             (1.2, 1.0, 0.75))
    for k in range(6):
        x = rng.uniform(-0.8, 0.8)
        blob("snow_%d" % k, rng.uniform(0.14, 0.20), (x, rng.uniform(0.05, 0.35), 0.24 + (0.9 - abs(x)) * 0.10),
             "qp_snow", (1.4, 1.0, 0.30))
    # The slab, at the head of it, toward the camera: grey, and no taller than
    # someone kneeling, so it reads as a stone and not a door.
    blk("slab", (0.58, 0.16, 0.82), (0, -0.30, 0.42), "qp_stone", bev=0.05)
    blk("slab_snow", (0.62, 0.20, 0.05), (0, -0.30, 0.85), "qp_snow", bev=0.02)
    # One column of lines, each its own length, as names cut one under the
    # other: two columns of even marks read as a window.
    for k in range(11):
        w = (0.30, 0.22, 0.34, 0.26, 0.20, 0.32, 0.24, 0.30, 0.18, 0.28, 0.24)[k]
        blk("name_%d" % k, (w, 0.04, 0.035), (0, -0.39, 0.77 - k * 0.064), "qp_rune", bev=0)
    # The watch's broken banner pole, laid along the mound, and a rag of it.
    rod("pole", (-0.85, 0.15, 0.36), (0.55, 0.25, 0.42), 0.05, "qp_pole")
    blk("rag", (0.18, 0.04, 0.16), (-0.70, 0.12, 0.30), "qp_red_dk", bev=0)
    return 2.5


def prop_harl_trap():
    """One of Old Harl's traps: iron jaws standing up off a plate, chained to
    a stake, and a wooden tag on the chain with his mark burned in -- sprung,
    the jaws shut on nothing. Chunky: at forty pixels a thin jaw is a line."""
    blk("plate", (0.62, 0.40, 0.06), (0, 0, 0.03), "qp_iron", bev=0.01, metal=0.4)
    # Two arcs of iron meeting at the top, a row of teeth along each.
    for side, y in ((-1, -0.05), (1, 0.05)):
        for k in range(9):
            a = math.radians(-80 + k * 20)
            x, z = math.sin(a) * 0.30, 0.06 + math.cos(a) * 0.30
            blk("jaw_%d_%d" % (side, k), (0.10, 0.06, 0.10), (x, y, z), "qp_iron_lt", rot=(0, a, 0), bev=0.01,
                metal=0.5)
            if 1 <= k <= 7:
                blk("tooth_%d_%d" % (side, k), (0.04, 0.05, 0.08), (x * 0.82, y, 0.06 + math.cos(a) * 0.24),
                    "qp_iron", rot=(0, a, 0), bev=0)
    blk("spring_l", (0.14, 0.14, 0.10), (-0.38, 0, 0.07), "qp_iron", bev=0.01)
    blk("spring_r", (0.14, 0.14, 0.10), (0.38, 0, 0.07), "qp_iron", bev=0.01)
    # The chain to the stake, and the tag on it.
    for k in range(4):
        blk("link_%d" % k, (0.10, 0.06, 0.06), (0.46 + k * 0.10, 0.10 + k * 0.03, 0.06), "qp_iron_lt", bev=0)
    frustum("stake", 0.08, 0.05, 0.50, (0.88, 0.22, 0.25), "qp_stake", verts=6)
    blk("tag", (0.22, 0.04, 0.26), (0.74, 0.08, 0.26), "qp_tag", bev=0.01)
    blk("tag_mark", (0.10, 0.04, 0.05), (0.74, 0.05, 0.28), "qp_rune", bev=0)
    return 1.15


# =================================================================================
#  Registration: name -> (builder, final pixels across)
# =================================================================================
PROPS = {
    "guild_cairn":      (prop_guild_cairn, 56),
    "spirewatch_cairn": (prop_spirewatch_cairn, 104),
    "harl_trap":        (prop_harl_trap, 40),
}
