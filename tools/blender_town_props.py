# =============================================================================
#  blender_town_props.py - the towns rebuilt, and the woods that feed them.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only townhouse_a,birch_tree
#
#  Built with blender_props.py's own tools and registered into its PROPS
#  table, the way blender_farm_props.py is: that file hands itself over.
#
#  What is here:
#    - the four woods' trees and stumps: birch for the Whisperwood (the oak and
#      the pine are the CraftPix trees; the swamp tree and the charred tree are
#      in blender_props.py already), and a stump for each that has none;
#    - Havenbrook: two-storey townhouses in four fronts, the mayor's hall, the
#      town well as the square's centrepiece, a street lamp and planters;
#    - Mossvale: a brick wall and its gate piers, and the dwarves' mine.
#
#  One Blender unit is about thirty-two pixels in every building here, the
#  same as the inn and the guild house they stand beside, so a townhouse is
#  plainly smaller than the hall and plainly bigger than a stall.
# =============================================================================

import math
import random

import bpy  # noqa: F401

import blender_props as bp

blk, cyl, cone, sphere = bp.blk, bp.cyl, bp.cone, bp.sphere
window, gable_roof = bp.window, bp.gable_roof
BUILDING_ELEVATION = bp.BUILDING_ELEVATION

bp.PALETTE.update({
    # Birch: bark that is nearly white, broken by black lenticels and scars,
    # and leaves a lighter, yellower green than the oak's.
    "birch_bark":    (0.900, 0.880, 0.820),
    "birch_bark_dk": (0.760, 0.740, 0.690),
    "birch_scar":    (0.150, 0.140, 0.130),
    "birch_leaf":    (0.560, 0.690, 0.290),
    "birch_leaf_lt": (0.700, 0.790, 0.360),
    "birch_leaf_dk": (0.400, 0.540, 0.220),
    "wood_cut":      (0.860, 0.760, 0.560),
    "wood_cut_dk":   (0.660, 0.540, 0.360),
})


# --- the woods ------------------------------------------------------------------

def _birch_stem(name, x, y, h, lean, r=0.075):
    """One white stem, leaning, with black scars round it at intervals."""
    ax, ay = lean
    cyl(name, r, h, (x + ax * h / 2, y + ay * h / 2, h / 2), "birch_bark",
        rot=(-ay * 1.0, ax * 1.0, 0), verts=10)
    rng = random.Random(hash(name) & 0xffff)
    k = 0
    z = 0.25
    while z < h - 0.2:
        # The scars are bands and dashes on the face the camera sees.
        w = rng.uniform(0.06, 0.12)
        blk("%s_scar_%d" % (name, k), (w, 0.03, 0.035),
            (x + ax * z + rng.uniform(-0.03, 0.03), y + ay * z - r * 0.92, z),
            "birch_scar", bev=0)
        z += rng.uniform(0.16, 0.30)
        k += 1
    return (x + ax * h, y + ay * h, h)


def prop_birch_tree():
    """A clump of three white stems under a light, open crown.

    Birch has to read as birch at a hundred pixels, which is the white bark
    and nothing else: an oak's crown with a pale trunk is a sick oak. So the
    stems are three, thin, leaning apart, long enough to show between the
    crown's lobes, and scarred black; and the crown is several small loose
    clumps in a yellow-green rather than one dark ball.
    """
    rng = random.Random(41)
    tops = [
        _birch_stem("stem_a", -0.10, 0.00, 2.45, (-0.10, 0.02), 0.085),
        _birch_stem("stem_b", 0.12, 0.04, 2.15, (0.12, -0.02), 0.075),
        _birch_stem("stem_c", 0.02, -0.12, 1.75, (0.03, -0.05), 0.065),
    ]
    # Roots flaring into the ground, and a scatter of leaf litter.
    for k in range(4):
        a = k * 1.57 + 0.4
        cyl("root_%d" % k, 0.05, 0.34, (math.cos(a) * 0.14, math.sin(a) * 0.14, 0.05), "birch_bark_dk",
            rot=(math.sin(a) * 1.2, -math.cos(a) * 1.2, 0), verts=8)
    # Thin twigs off each stem up into the crown.
    for i, (tx, ty, tz) in enumerate(tops):
        for k in range(3):
            a = rng.uniform(0, math.tau)
            ln = rng.uniform(0.35, 0.55)
            cyl("twig_%d_%d" % (i, k), 0.022, ln,
                (tx + math.cos(a) * ln * 0.3, ty + math.sin(a) * ln * 0.3, tz - 0.35 + k * 0.12),
                "birch_bark_dk", rot=(math.sin(a) * -0.9, math.cos(a) * 0.9, 0), verts=6)
    # The crown: loose clumps of leaf round and above the stem tops, three
    # greens so it has a lit side and a shaded one.
    greens = ("birch_leaf", "birch_leaf_lt", "birch_leaf_dk")
    for i, (tx, ty, tz) in enumerate(tops):
        for k in range(22):
            a = rng.uniform(0, math.tau)
            d = rng.uniform(0.05, 0.50)
            x = tx + math.cos(a) * d
            y = ty + math.sin(a) * d * 0.7
            z = tz - 0.40 + rng.uniform(-0.05, 0.55)
            s = rng.uniform(0.09, 0.16)
            # Lit on the upper left, where the key light is.
            tone = greens[1] if (x < tx and z > tz) else greens[2] if (x > tx + 0.1 or z < tz - 0.15) else greens[0]
            sphere("leaf_%d_%d" % (i, k), s, (x, y, z), tone)
    return 3.3


def _stump(prefix, top_col, bark_col, ring_col, r=0.26, h=0.30, seed=5):
    rng = random.Random(seed)
    cyl(prefix + "_body", r, h, (0, 0, h / 2), bark_col, verts=12)
    cyl(prefix + "_top", r - 0.03, 0.03, (0, 0, h + 0.005), top_col, verts=16)
    for k in range(2):
        cyl("%s_ring_%d" % (prefix, k), (r - 0.06) * (0.62 - k * 0.3), 0.012, (0, 0, h + 0.022), ring_col, verts=14)
    for k in range(5):
        a = k / 5 * math.tau + rng.uniform(-0.2, 0.2)
        cyl("%s_root_%d" % (prefix, k), 0.06, 0.28, (math.cos(a) * (r + 0.02), math.sin(a) * (r + 0.02), 0.05),
            bark_col, rot=(math.sin(a) * 1.25, -math.cos(a) * 1.25, 0), verts=8)
    # The axe's last cut: a wedge chipped out of the front and a splinter.
    blk(prefix + "_chip", (0.16, 0.08, 0.10), (0.06, -r + 0.02, h - 0.04), top_col,
        rot=(math.radians(30), 0, 0), bev=0.01)


def prop_stump_birch():
    """A birch cut down: white bark round pale wood."""
    _stump("stump", "wood_cut", "birch_bark", "wood_cut_dk")
    for k in range(3):
        blk("scar_%d" % k, (0.09, 0.02, 0.03), (-0.12 + k * 0.12, -0.255, 0.10 + (k % 2) * 0.1), "birch_scar", bev=0)
    return 1.1


def prop_stump_charred():
    """An ashen tree cut down: black round a heart still glowing."""
    _stump("stump", "char_lt", "char", "ember", seed=9)
    blk("ember_heart", (0.08, 0.08, 0.02), (0, 0, 0.33), "ember", emit=1.2, bev=0)
    return 1.1


# --- Havenbrook: the townhouses --------------------------------------------------

bp.PALETTE.update({
    "roof_tile":     (0.640, 0.310, 0.220),
    "roof_tile_dk":  (0.470, 0.215, 0.160),
    "roof_tile_lt":  (0.740, 0.400, 0.290),
    "plaster_ochre": (0.870, 0.720, 0.470),
    "plaster_rose":  (0.870, 0.690, 0.640),
    "plaster_sky":   (0.700, 0.780, 0.820),
    "brick_red":     (0.640, 0.310, 0.240),
    "brick_red_lt":  (0.720, 0.390, 0.300),
    "brick_brown":   (0.500, 0.330, 0.250),
    "brick_brown_lt":(0.580, 0.400, 0.300),
    "mortar":        (0.740, 0.700, 0.620),
    "door_green":    (0.230, 0.420, 0.300),
    "door_blue":     (0.230, 0.330, 0.560),
    "door_red":      (0.580, 0.200, 0.170),
    "door_ochre":    (0.700, 0.520, 0.200),
    "geranium":      (0.860, 0.200, 0.200),
    "marigold_f":    (0.960, 0.700, 0.160),
    "petal_pink":    (0.930, 0.560, 0.700),
    "petal_white":   (0.950, 0.940, 0.900),
})


def _bricks(name, x0, x1, z0, z1, y, tones, rng, gap=None, course=0.105, brick=0.24):
    """A face of brick in stretcher bond between x0..x1 and z0..z1, standing
    in front of mortar at depth y. Bricks are chunkier than life (a real one
    is a pixel tall here) so the bond shows at all."""
    blk(name + "_mortar", (x1 - x0, 0.04, z1 - z0), ((x0 + x1) / 2, y + 0.03, (z0 + z1) / 2), "mortar", bev=0)
    rows = int((z1 - z0) / course)
    for row in range(rows):
        z = z0 + course / 2 + row * course
        x = x0 - (brick / 2 if row % 2 else 0.0)
        while x < x1 - 0.02:
            a, b = max(x, x0), min(x + brick, x1)
            cx = (a + b) / 2
            if b - a > 0.05 and not (gap and gap(cx, z)):
                blk("%s_%d_%.2f" % (name, row, x), (b - a - 0.025, 0.07, course - 0.022), (cx, y, z),
                    tones[rng.randrange(len(tones))], bev=0.008)
            x += brick


def _timber_face(name, x0, x1, zb, zt, y, braces=True, mid=True):
    """The dark lattice of a timber frame on a plaster face: posts at the
    corners and between the windows, rails at the sill and head, and braces
    across the end panels."""
    w = x1 - x0
    posts = [x0, x0 + w * 0.22, x0 + w * 0.5, x1 - w * 0.22, x1] if mid else [x0, x0 + w * 0.22, x1 - w * 0.22, x1]
    for x in posts:
        blk("%s_post_%.2f" % (name, x), (0.09, 0.06, zt - zb), (x, y, (zb + zt) / 2), "oak", bev=0.005)
    for z in (zb + 0.05, zb + (zt - zb) * 0.34, zt - 0.05):
        blk("%s_rail_%.2f" % (name, z), (w + 0.09, 0.06, 0.08), ((x0 + x1) / 2, y, z), "oak", bev=0.005)
    if braces:
        for i, (a, b) in enumerate(((x0, x0 + w * 0.22), (x1 - w * 0.22, x1))):
            h = (zt - zb) * 0.34
            ln = math.hypot(b - a, h)
            ang = math.atan2(h, b - a) * (1 if i == 0 else -1)
            blk("%s_brace_%d" % (name, i), (ln, 0.06, 0.07), ((a + b) / 2, y, zb + h / 2), "oak",
                rot=(0, -ang, 0), bev=0.005)


def _door(name, x, y, z0, colour, w=0.60, h=0.98, fanlight=True):
    blk(name, (w, 0.08, h), (x, y, z0 + h / 2), colour)
    for k in (-1, 1):
        blk("%s_panel_%d" % (name, k), (w * 0.34, 0.03, h * 0.30), (x + k * w * 0.2, y - 0.05, z0 + h * 0.30),
            colour, bev=0.01)
        blk("%s_panel_hi_%d" % (name, k), (w * 0.34, 0.03, h * 0.30), (x + k * w * 0.2, y - 0.05, z0 + h * 0.68),
            colour, bev=0.01)
    sphere(name + "_knob", 0.04, (x + w * 0.32, y - 0.07, z0 + h * 0.50), "brass")
    blk(name + "_frame", (w + 0.14, 0.10, 0.10), (x, y - 0.02, z0 + h + 0.05), "stone_pale")
    for k in (-1, 1):
        blk("%s_jamb_%d" % (name, k), (0.08, 0.10, h), (x + k * (w / 2 + 0.03), y - 0.02, z0 + h / 2), "stone_pale")
    if fanlight:
        blk(name + "_fan", (w - 0.08, 0.05, 0.16), (x, y - 0.01, z0 + h + 0.20), "glass_lit", emit=0.5, bev=0)
        blk(name + "_fan_frame", (w + 0.10, 0.08, 0.06), (x, y - 0.03, z0 + h + 0.31), "stone_pale")
    blk(name + "_step", (w + 0.36, 0.34, 0.08), (x, y - 0.20, 0.04), "stone_pale")


def _flower_box(name, x, y, z, w=0.50, tones=("geranium", "petal_white", "geranium", "marigold_f")):
    blk(name, (w, 0.16, 0.12), (x, y - 0.08, z), "oak_light")
    n = max(3, int(w / 0.12))
    for k in range(n):
        sphere("%s_leaf_%d" % (name, k), 0.06, (x - w / 2 + 0.06 + k * (w - 0.12) / (n - 1), y - 0.10, z + 0.08), "leaf")
        sphere("%s_bloom_%d" % (name, k), 0.045, (x - w / 2 + 0.06 + k * (w - 0.12) / (n - 1), y - 0.16, z + 0.12),
               tones[k % len(tones)])


def _front_gable(name, W, D, eave, pitch_deg, colour, face_colour, over=0.20, thick=0.12):
    """A roof whose gable end faces the street: two slabs sloping down to
    the left and right, and the triangle of wall between them facing the
    camera. The ridge runs back into the picture."""
    pitch = math.radians(pitch_deg)
    half = W / 2 + over
    slab = half / math.cos(pitch)
    rise = half * math.tan(pitch)
    for side in (-1, 1):
        blk("%s_slab_%d" % (name, side), (slab, D + over * 2, thick), (side * half / 2, 0, eave + rise / 2),
            colour, rot=(0, side * pitch, 0), bev=0.02)
        # Barge boards along the front edge of each slope.
        blk("%s_barge_%d" % (name, side), (slab, 0.08, 0.12), (side * half / 2, -D / 2 - over - 0.02, eave + rise / 2 - 0.02),
            "oak", rot=(0, side * pitch, 0), bev=0.01)
    bp._cloth_shape(name + "_face", [(-W / 2, eave), (W / 2, eave), (0, eave + (W / 2) * math.tan(pitch))],
                    -D / 2 + 0.02, D - 0.04, face_colour)
    return rise


STYLES = {
    # Brick below and a cream timber frame above, red tile, green door.
    "a": dict(ground="brick", bricks=("brick_red", "brick_red_lt", "brick_red"), upper="timber",
              plaster="plaster", roof="side", roof_col="roof_tile", roof_dk="roof_tile_dk",
              door="door_green", boxes=True, chimney=1, dormer=False, sign=None, seed=11),
    # Stone below, ochre frame above and jettied out, slate, blue door, a baker's sign.
    "b": dict(ground="stone", upper="timber", plaster="plaster_ochre", roof="side", roof_col="slate",
              roof_dk="slate_dk", door="door_blue", boxes=False, chimney=-1, dormer=True, sign="bread",
              jetty=0.16, seed=12),
    # Gable end to the street: dark brick and a white frame with its triangle, shingle, red door.
    "c": dict(ground="brick", bricks=("brick_brown", "brick_brown_lt", "brick_brown"), upper="timber",
              plaster="plaster", roof="front", roof_col="shingle", roof_dk="shingle_dk", door="door_red",
              boxes=True, chimney=0, dormer=False, sign=None, seed=13),
    # Brick all the way up, rose-washed stone trim, two dormers in tile, ochre door.
    "d": dict(ground="brick", bricks=("brick_red", "brick_brown_lt", "brick_red_lt"), upper="brick",
              plaster="plaster_rose", roof="side", roof_col="roof_tile_lt", roof_dk="roof_tile_dk",
              door="door_ochre", boxes=True, chimney=1, dormer=True, sign=None, seed=14),
}


def _townhouse(style):
    """A two-storey townhouse for Havenbrook's streets, in one of four fronts.

    Narrow, tall and close-set, where the cottages it replaces were low and
    wide: two storeys of windows over a door, a chimney, and a roof of its own
    colour, so a street of them reads as a street and not as one house copied.
    The door is at the image's centre, where PlaceBuilding cuts a doorway."""
    st = STYLES[style]
    rng = random.Random(st["seed"])
    # Shallow from front to back and tall: seen from above, a deep house is
    # nearly all roof, and the two storeys are what makes it a townhouse.
    W, D = 2.50, 1.50
    G, U = 1.32, 1.26
    JET = st.get("jetty", 0.0)
    fy = -D / 2
    uy = fy - JET
    zb = G + 0.12
    EAVE = zb + U

    blk("plinth", (W + 0.10, D + 0.10, 0.10), (0, 0, 0.05), "stone_pale")
    blk("ground_core", (W - 0.04, D - 0.10, G), (0, 0.04, G / 2), "stone" if st["ground"] == "stone" else "brick_dark",
        bev=0)

    win_xs = (-0.78, 0.78)
    def ground_gap(cx, z):
        if abs(cx) < 0.40 and z < 1.20:
            return True
        return any(abs(cx - wx) < 0.30 and 0.38 < z < 1.08 for wx in win_xs)
    if st["ground"] == "brick":
        _bricks("gbrick", -W / 2, W / 2, 0.10, G, fy - 0.01, st["bricks"], rng, gap=ground_gap)
    else:
        bp._coursed("gstone", -W / 2, W / 2, 0.10, G, fy - 0.01, ("stone", "stone_pale", "stone"), rng,
                    gap=ground_gap)
    for wx in win_xs:
        window("gwin_%.2f" % wx, wx, fy - 0.04, 0.74, w=0.46, h=0.58, shutters=(style in "ad"),
               lit=(wx < 0 or style != "c"))
    _door("door", 0, fy - 0.04, 0.10, st["door"])

    # --- the upper storey ---------------------------------------------------------
    blk("floor_beam", (W + 0.18 + JET, D + JET + 0.10, 0.12), (0, -JET / 2, G + 0.06), "oak")
    if st["upper"] == "timber":
        blk("upper_core", (W + 0.08, D + JET, U), (0, -JET / 2, zb + U / 2), st["plaster"], bev=0.01)
        _timber_face("frame", -W / 2 - 0.04, W / 2 + 0.04, zb, EAVE, uy - 0.03)
    else:
        blk("upper_core", (W - 0.04, D - 0.10, U), (0, 0.04, zb + U / 2), "brick_dark", bev=0)
        _bricks("ubrick", -W / 2, W / 2, zb, EAVE, uy - 0.01, st["bricks"], rng,
                gap=lambda cx, z: any(abs(cx - wx) < 0.26 and zb + 0.30 < z < zb + 0.94 for wx in (-0.70, 0.0, 0.70)))
        blk("string", (W + 0.10, 0.12, 0.08), (0, uy - 0.04, zb + 0.02), st["plaster"])
        blk("cornice", (W + 0.14, 0.14, 0.10), (0, uy - 0.04, EAVE - 0.04), st["plaster"])
    upper_xs = (-0.70, 0.0, 0.70) if st["upper"] == "brick" or st["roof"] == "front" else (-0.62, 0.62)
    for k, wx in enumerate(upper_xs):
        window("uwin_%d" % k, wx, uy - 0.06, zb + 0.62, w=0.40, h=0.52, shutters=(st["upper"] == "brick"),
               lit=(k != 1))
        if st["upper"] == "brick":
            blk("uwin_lintel_%d" % k, (0.52, 0.10, 0.08), (wx, uy - 0.05, zb + 0.94), st["plaster"])
        if st["boxes"]:
            _flower_box("ubox_%d" % k, wx, uy - 0.05, zb + 0.30, w=0.50)

    # --- the roof ---------------------------------------------------------------------
    if st["roof"] == "side":
        rise = gable_roof("roof", W + 0.08, D + JET, EAVE, 40, st["roof_col"], overhang=0.22)
        pitch = math.radians(40)
        half = (D + JET) / 2 + 0.22
        for k in range(1, 5):
            t = k / 5.0
            blk("course_%d" % k, (W + 0.52, 0.05, 0.05), (0, -half * (1 - t) - JET / 2 - 0.05, EAVE + rise * t + 0.07),
                st["roof_dk"], rot=(pitch, 0, 0), bev=0)
        if st["dormer"]:
            for k, dx in enumerate((0.0,) if style == "b" else (-0.62, 0.62)):
                dz = EAVE + rise * 0.40
                dy = -half * 0.60 - JET / 2
                blk("dormer_%d" % k, (0.56, 0.26, 0.52), (dx, dy - 0.02, dz + 0.18), st["plaster"])
                window("dormer_win_%d" % k, dx, dy - 0.16, dz + 0.18, w=0.26, h=0.28, shutters=False)
                for side in (-1, 1):
                    blk("dormer_roof_%d_%d" % (k, side), (0.40, 0.46, 0.06), (dx + side * 0.17, dy + 0.02, dz + 0.52),
                        st["roof_dk"], rot=(0, side * math.radians(42), 0), bev=0.01)
        chim_x = st["chimney"] * (W / 2 - 0.40)
    else:
        rise = _front_gable("roof", W + 0.08, D + JET, EAVE, 42, st["roof_col"], st["plaster"])
        # The gable triangle is timbered too: a king post and two struts, and
        # a small window in the peak.
        gz = EAVE
        blk("gable_tie", (W + 0.10, 0.07, 0.08), (0, -D / 2 - JET - 0.03, gz + 0.04), "oak", bev=0.005)
        blk("gable_king", (0.08, 0.07, rise * 0.9), (0, -D / 2 - JET - 0.03, gz + rise * 0.45), "oak", bev=0.005)
        for side in (-1, 1):
            ln = math.hypot(W * 0.26, rise * 0.5)
            blk("gable_strut_%d" % side, (ln, 0.07, 0.07), (side * W * 0.13, -D / 2 - JET - 0.03, gz + rise * 0.25),
                "oak", rot=(0, side * math.atan2(rise * 0.5, W * 0.26), 0), bev=0.005)
            window("gable_win_%d" % side, side * 0.40, -D / 2 - JET - 0.06, gz + 0.34, w=0.24, h=0.26, shutters=False,
                   lit=(side < 0))
        chim_x = W / 2 - 0.36
    if st["chimney"] != 0 or st["roof"] == "front":
        cz0 = EAVE + rise * 0.35
        ch = rise * 0.85 + 0.30
        cy_ = -0.10 if st["roof"] == "side" else 0.20
        blk("chimney", (0.36, 0.36, ch), (chim_x, cy_, cz0 + ch / 2), "brick_dark" if st["ground"] == "brick" else "stone")
        blk("chimney_cap", (0.46, 0.46, 0.08), (chim_x, cy_, cz0 + ch + 0.04), "stone_pale")
        blk("chimney_pot", (0.16, 0.16, 0.16), (chim_x, cy_, cz0 + ch + 0.16), "clay")

    # --- a trade's sign ---------------------------------------------------------------
    if st["sign"] == "bread":
        sx = -W / 2 - 0.06
        blk("sign_bracket", (0.66, 0.06, 0.06), (sx - 0.22, uy - 0.10, zb + 0.30), "iron", metal=0.7)
        blk("sign_board", (0.50, 0.05, 0.40), (sx - 0.36, uy - 0.10, zb + 0.02), "cloth_cream")
        blk("sign_edge", (0.56, 0.04, 0.46), (sx - 0.36, uy - 0.07, zb + 0.02), "oak_light")
        cyl("sign_loaf", 0.12, 0.30, (sx - 0.36, uy - 0.14, zb + 0.02), "oak_pale", rot=(0, math.pi / 2, 0), verts=12)
        for k in range(3):
            blk("sign_slash_%d" % k, (0.03, 0.03, 0.14), (sx - 0.44 + k * 0.08, uy - 0.25, zb + 0.06), "oak_light",
                rot=(0, math.radians(25), 0), bev=0)
    # The gable end stands taller than a side roof: framed wider, and
    # rendered bigger to match, so every house is at the same scale.
    return (4.2 if st["roof"] == "side" else 5.13, BUILDING_ELEVATION)


def prop_townhouse_a():
    return _townhouse("a")


def prop_townhouse_b():
    return _townhouse("b")


def prop_townhouse_c():
    return _townhouse("c")


def prop_townhouse_d():
    return _townhouse("d")


# --- Havenbrook: the mayor's hall -------------------------------------------------

bp.PALETTE.update({
    "limestone":     (0.860, 0.820, 0.720),
    "limestone_dk":  (0.740, 0.700, 0.610),
    "limestone_lt":  (0.920, 0.890, 0.810),
    "verdigris":     (0.400, 0.620, 0.540),
    "verdigris_dk":  (0.290, 0.470, 0.410),
    "flag_blue":     (0.200, 0.320, 0.620),
    "flag_gold":     (0.900, 0.720, 0.240),
    "clock_face":    (0.950, 0.930, 0.860),
})


def prop_mayor_hall():
    """Havenbrook's hall, where the mayor sits: the civic face of the town.

    Pale limestone where the guild is dark stone, a copper roof gone green
    where the guild's is slate, and a columned porch with a pediment where
    the guild has a tower -- so the two big buildings on the square answer
    each other without looking alike. A clock in a cupola on the ridge, the
    town's flags either side of the steps, and every window lit: the hall
    keeps late hours."""
    rng = random.Random(90)
    W, D = 5.40, 2.30
    G, U = 1.50, 1.36
    EAVE = G + U + 0.10
    fy = -D / 2
    PD = 0.66                    # how far the porch stands out
    py = fy - PD

    blk("core", (W - 0.02, D - 0.10, EAVE), (0, 0.04, EAVE / 2), "limestone_dk", bev=0)
    blk("plinth", (W + 0.16, D + 0.16, 0.16), (0, 0, 0.08), "limestone_dk")
    win_x = (-2.15, -1.45, 1.45, 2.15)
    def gap(cx, z):
        if abs(cx) < 0.66 and z < 1.60:
            return True
        return any(abs(cx - wx) < 0.27 and (0.50 < z < 1.34 or G + 0.30 < z < G + 1.10) for wx in win_x)
    bp._coursed("wall", -W / 2, W / 2, 0.16, EAVE - 0.06, fy, ("limestone", "limestone_lt", "limestone"), rng,
                course=0.26, gap=gap)
    blk("string", (W + 0.12, 0.18, 0.12), (0, fy - 0.04, G + 0.06), "limestone_lt")
    blk("cornice", (W + 0.22, 0.26, 0.16), (0, fy - 0.06, EAVE - 0.02), "limestone_lt")
    for sx in (-1, 1):
        # Pilasters at the corners, rusticated.
        for k in range(int(EAVE / 0.26)):
            wq = 0.32 if k % 2 else 0.24
            blk("quoin_%d_%d" % (sx, k), (wq, 0.12, 0.23), (sx * (W / 2 - wq / 2 + 0.02), fy - 0.02, 0.29 + k * 0.26),
                "limestone_lt", bev=0.02)
    for wx in win_x:
        window("gwin_%.2f" % wx, wx, fy - 0.02, 0.90, w=0.42, h=0.76, shutters=False)
        bp._arch("gwin_arch_%.2f" % wx, wx, fy - 0.03, 1.28, 0.42, "limestone_lt", stones=5, thick=0.12)
        window("uwin_%.2f" % wx, wx, fy - 0.02, G + 0.70, w=0.42, h=0.72, shutters=False, lit=(wx != -1.45))
        blk("uwin_pediment_%.2f" % wx, (0.62, 0.14, 0.08), (wx, fy - 0.06, G + 1.12), "limestone_lt")
        # A little iron balcony under each upper window.
        blk("balc_%.2f" % wx, (0.60, 0.20, 0.05), (wx, fy - 0.12, G + 0.26), "limestone_lt")
        for k in range(5):
            blk("balc_bar_%.2f_%d" % (wx, k), (0.025, 0.025, 0.20), (wx - 0.24 + k * 0.12, fy - 0.20, G + 0.38),
                "iron", metal=0.6, bev=0)
        blk("balc_rail_%.2f" % wx, (0.60, 0.04, 0.04), (wx, fy - 0.20, G + 0.48), "iron", metal=0.6, bev=0)

    # Banners of the town between the windows, blue with a gold band.
    for sx in (-1, 1):
        x = sx * 1.80
        blk("banner_rod_%d" % sx, (0.40, 0.06, 0.05), (x, fy - 0.10, EAVE - 0.24), "brass", metal=0.7)
        blk("banner_%d" % sx, (0.32, 0.04, 1.00), (x, fy - 0.10, EAVE - 0.76), "flag_blue", bev=0.01)
        blk("banner_band_%d" % sx, (0.32, 0.05, 0.08), (x, fy - 0.13, EAVE - 0.50), "flag_gold", metal=0.3)
        blk("banner_key_%d" % sx, (0.10, 0.05, 0.22), (x, fy - 0.13, EAVE - 0.80), "flag_gold", metal=0.3)

    # --- the porch: steps, four columns, an entablature and a pediment ---------------
    for k, (w, d) in enumerate(((2.70, 0.96), (2.44, 0.78), (2.18, 0.60))):
        blk("step_%d" % k, (w, d, 0.09), (0, fy - d / 2 + 0.02, 0.045 + k * 0.09), "limestone_lt")
    blk("porch_floor", (2.10, PD + 0.10, 0.06), (0, fy - PD / 2, 0.30), "limestone")
    col_x = (-0.96, -0.52, 0.52, 0.96)
    CH = G + 0.72
    for cx in col_x:
        blk("col_base_%.2f" % cx, (0.30, 0.30, 0.12), (cx, py + 0.14, 0.39), "limestone_lt")
        cyl("col_%.2f" % cx, 0.12, CH - 0.36, (cx, py + 0.14, 0.45 + (CH - 0.36) / 2), "limestone_lt", verts=14)
        for f in (-1, 0, 1):
            blk("col_flute_%.2f_%d" % (cx, f), (0.018, 0.02, CH - 0.44), (cx + f * 0.06, py + 0.02, 0.45 + (CH - 0.36) / 2),
                "limestone_dk", bev=0)
        blk("col_cap_%.2f" % cx, (0.32, 0.32, 0.12), (cx, py + 0.14, CH + 0.03), "limestone_lt")
    blk("entablature", (2.30, PD + 0.30, 0.30), (0, fy - PD / 2 + 0.02, CH + 0.24), "limestone_lt")
    blk("frieze", (2.24, 0.05, 0.10), (0, py - 0.14, CH + 0.22), "limestone_dk", bev=0)
    ped_h = 0.62
    bp._cloth_shape("pediment", [(-1.18, CH + 0.39), (1.18, CH + 0.39), (0, CH + 0.39 + ped_h)],
                    py - 0.13, PD + 0.40, "limestone")
    for side in (-1, 1):
        ln = math.hypot(1.26, ped_h)
        blk("ped_edge_%d" % side, (ln, PD + 0.46, 0.10), (side * 0.60, fy - PD / 2 + 0.04, CH + 0.39 + ped_h / 2 + 0.02),
            "verdigris_dk", rot=(0, side * math.atan2(ped_h, 1.26), 0), bev=0.01)
    # The town's arms in the pediment: a gold key on a blue shield.
    blk("arms", (0.32, 0.05, 0.30), (0, py - 0.16, CH + 0.62), "flag_blue", bev=0.03)
    blk("arms_key", (0.06, 0.06, 0.22), (0, py - 0.19, CH + 0.62), "flag_gold", metal=0.5)
    blk("arms_bit", (0.12, 0.06, 0.05), (0.05, py - 0.19, CH + 0.54), "flag_gold", metal=0.5)
    # The doors behind the columns: tall, double, dark green, under a fanlight.
    blk("door_l", (0.46, 0.08, 1.22), (-0.24, fy - 0.02, 0.36 + 0.61), "door_green")
    blk("door_r", (0.46, 0.08, 1.22), (0.24, fy - 0.02, 0.36 + 0.61), "door_green")
    blk("door_seam", (0.03, 0.10, 1.22), (0, fy - 0.06, 0.97), "oak")
    for sx in (-1, 1):
        for zz in (0.70, 1.20):
            blk("door_panel_%d_%.1f" % (sx, zz), (0.30, 0.03, 0.36), (sx * 0.24, fy - 0.07, zz), "door_green", bev=0.01)
        sphere("door_knob_%d" % sx, 0.04, (sx * 0.08, fy - 0.08, 0.96), "brass")
    blk("fanlight", (0.86, 0.05, 0.20), (0, fy - 0.02, 1.72), "glass_lit", emit=0.6, bev=0)
    bp._arch("door_arch", 0, fy - 0.03, 1.62, 0.96, "limestone_lt", stones=9, thick=0.14)
    # Lamps on the outer columns.
    for sx in (-1, 1):
        blk("lamp_%d" % sx, (0.14, 0.14, 0.22), (sx * 0.96, py - 0.08, 1.36), "candle_glow", emit=1.4)
        blk("lamp_cap_%d" % sx, (0.18, 0.18, 0.05), (sx * 0.96, py - 0.08, 1.49), "iron", metal=0.6)
        blk("lamp_arm_%d" % sx, (0.05, 0.16, 0.05), (sx * 0.96, py + 0.02, 1.48), "iron", metal=0.6)

    # --- the roof, and the clock over it ---------------------------------------------------
    rise = gable_roof("roof", W + 0.22, D, EAVE, 36, "verdigris", thick=0.12, overhang=0.24)
    pitch = math.radians(36)
    half = D / 2 + 0.24
    for k in range(1, 6):
        t = k / 6.0
        blk("seam_%d" % k, (W + 0.70, 0.05, 0.04), (0, -half * (1 - t) - 0.05, EAVE + rise * t + 0.07),
            "verdigris_dk", rot=(pitch, 0, 0), bev=0)
    # Standing seams down the slope, the way a copper roof is laid.
    for k in range(-6, 7):
        blk("rib_%d" % k, (0.04, half / math.cos(pitch), 0.04), (k * 0.44, -half / 2, EAVE + rise / 2 + 0.07),
            "verdigris_dk", rot=(-pitch, 0, 0), bev=0)
    # The cupola: a square base rising out of the ridge with a clock on its
    # face, an open belfry of four posts, and a green dome with a vane.
    CB = EAVE + rise * 0.45
    cyb = -0.30
    blk("cupola_base", (1.00, 0.90, rise * 0.55 + 0.80), (0, cyb, CB + (rise * 0.55 + 0.80) / 2), "limestone")
    top = CB + rise * 0.55 + 0.80
    blk("cupola_cornice", (1.14, 1.04, 0.10), (0, cyb, top + 0.05), "limestone_lt")
    cyl("clock_rim", 0.34, 0.06, (0, cyb - 0.46, top - 0.40), "flag_gold", rot=(math.pi / 2, 0, 0), verts=24, metal=0.5)
    cyl("clock", 0.29, 0.06, (0, cyb - 0.48, top - 0.40), "clock_face", rot=(math.pi / 2, 0, 0), verts=24)
    for k in range(12):
        a = k / 12 * math.tau
        blk("tick_%d" % k, (0.03, 0.02, 0.05), (math.sin(a) * 0.23, cyb - 0.52, top - 0.40 + math.cos(a) * 0.23),
            "iron", rot=(0, -a, 0), bev=0)
    blk("hand_hour", (0.035, 0.02, 0.15), (0.05, cyb - 0.53, top - 0.36), "iron", rot=(0, math.radians(-40), 0), bev=0)
    blk("hand_min", (0.03, 0.02, 0.22), (0, cyb - 0.54, top - 0.30), "iron", bev=0)
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("belfry_post_%d_%d" % (sx, sy), (0.12, 0.12, 0.52), (sx * 0.36, cyb + sy * 0.30, top + 0.36), "limestone_lt")
    cone("bell", 0.16, 0.26, (0, cyb, top + 0.30), "brass", verts=12)
    blk("belfry_top", (0.96, 0.86, 0.10), (0, cyb, top + 0.66), "limestone_lt")
    sphere("dome", 0.44, (0, cyb, top + 0.70), "verdigris")
    blk("dome_cut", (1.0, 1.0, 0.44), (0, cyb, top + 0.49), "limestone_lt", bev=0)
    cyl("vane_pole", 0.025, 0.54, (0, cyb, top + 1.30), "iron", verts=8, metal=0.6)
    sphere("vane_ball", 0.06, (0, cyb, top + 1.14), "flag_gold")
    blk("vane_arrow", (0.46, 0.03, 0.05), (0.04, cyb, top + 1.44), "iron", metal=0.6)
    blk("vane_tail", (0.12, 0.03, 0.14), (-0.18, cyb, top + 1.44), "iron", metal=0.6)
    # Chimneys at the ends, on the front slope.
    for sx in (-1, 1):
        cz0 = EAVE + rise * 0.30
        ch = rise * 0.90 + 0.30
        blk("chimney_%d" % sx, (0.40, 0.40, ch), (sx * (W / 2 - 0.46), -0.20, cz0 + ch / 2), "limestone_dk")
        blk("chimney_cap_%d" % sx, (0.52, 0.52, 0.10), (sx * (W / 2 - 0.46), -0.20, cz0 + ch + 0.05), "limestone_lt")

    # --- the flags either side of the steps --------------------------------------------------
    # Out past the corners, flying outward: in front of the face they hung
    # over the upper windows and read as more banners.
    for sx in (-1, 1):
        x = sx * (W / 2 + 0.22)
        blk("flag_foot_%d" % sx, (0.26, 0.26, 0.16), (x, fy - 0.30, 0.08), "limestone_lt")
        cyl("flag_pole_%d" % sx, 0.03, 3.3, (x, fy - 0.30, 1.80), "iron", verts=8, metal=0.6)
        sphere("flag_ball_%d" % sx, 0.05, (x, fy - 0.30, 3.47), "flag_gold")
        blk("flag_%d" % sx, (0.64, 0.03, 0.40), (x + sx * 0.34, fy - 0.30, 3.18), "flag_blue", bev=0)
        blk("flag_stripe_%d" % sx, (0.64, 0.035, 0.08), (x + sx * 0.34, fy - 0.31, 3.18), "flag_gold", bev=0)
    return (7.6, BUILDING_ELEVATION)


# --- Havenbrook: the square ----------------------------------------------------------

def prop_town_well():
    """The town well as the square's centrepiece: dry, as it has been for
    eleven years, and still the middle of the town.

    The old well was a cottage well at fifty pixels, lost in a paved square.
    This one stands on two round steps inside a low basin wall -- the trough
    the water used to spill into -- with four stone posts carrying a shingled
    canopy, a winch with its rope and bucket, and the planks nailed over the
    mouth that say what Bess is about to."""
    rng = random.Random(3)
    # The basin: a low ring wall of dressed stone round a dry, cracked floor.
    cyl("basin_floor", 1.30, 0.06, (0, 0, 0.03), "stone_pale", verts=40)
    for k in range(28):
        a = k / 28 * math.tau
        blk("basin_%d" % k, (0.30, 0.18, 0.26), (math.cos(a) * 1.30, math.sin(a) * 1.30, 0.13),
            ("stone", "stone_pale")[k % 2], rot=(0, 0, a + math.pi / 2), bev=0.02)
    cyl("basin_bed", 1.16, 0.04, (0, 0, 0.07), "earth_dk", verts=40)
    for k in range(9):
        a = rng.uniform(0, math.tau)
        r = rng.uniform(0.55, 1.05)
        blk("crack_%d" % k, (rng.uniform(0.12, 0.28), 0.02, 0.01), (math.cos(a) * r, math.sin(a) * r, 0.095),
            "void", rot=(0, 0, rng.uniform(0, math.pi)), bev=0)
    # Two steps up to the well.
    cyl("step_1", 0.86, 0.10, (0, 0, 0.12), "stone_pale", verts=32)
    cyl("step_2", 0.70, 0.10, (0, 0, 0.22), "stone", verts=32)
    # The well's own wall.
    for row in range(3):
        for k in range(14):
            a = (k + (0.5 if row % 2 else 0.0)) / 14 * math.tau
            blk("wall_%d_%d" % (row, k), (0.26, 0.16, 0.15), (math.cos(a) * 0.52, math.sin(a) * 0.52, 0.34 + row * 0.15),
                ("stone", "stone_pale", "rock")[rng.randrange(3)], rot=(0, 0, a + math.pi / 2), bev=0.02)
    cyl("rim", 0.60, 0.08, (0, 0, 0.78), "stone_pale", verts=28)
    cyl("mouth", 0.42, 0.10, (0, 0, 0.79), "void", verts=24)
    # Planks nailed across the mouth.
    for k, (x, a) in enumerate(((-0.18, 0.08), (0.04, -0.05), (0.22, 0.12))):
        blk("plank_%d" % k, (0.16, 0.96, 0.04), (x, 0, 0.86), "oak_light", rot=(0, 0, a), bev=0.01)
        for e in (-1, 1):
            sphere("nail_%d_%d" % (k, e), 0.018, (x + e * 0.4 * math.sin(a), e * 0.40, 0.885), "iron")
    # Posts and canopy.
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("post_%d_%d" % (sx, sy), (0.12, 0.12, 1.30), (sx * 0.46, sy * 0.30, 1.40), "stone_pale")
    blk("beam_l", (0.12, 0.80, 0.10), (-0.46, 0, 2.08), "oak")
    blk("beam_r", (0.12, 0.80, 0.10), (0.46, 0, 2.08), "oak")
    blk("beam_f", (1.10, 0.10, 0.10), (0, -0.34, 2.08), "oak")
    blk("beam_b", (1.10, 0.10, 0.10), (0, 0.34, 2.08), "oak")
    roof = cone("canopy", 0.98, 0.72, (0, 0, 2.48), "shingle", verts=4)
    roof.rotation_euler = (0, 0, math.radians(45))
    roof.scale = (1.0, 0.78, 1.0)
    cone("canopy_skirt", 1.02, 0.10, (0, 0, 2.16), "shingle_dk", verts=4).rotation_euler = (0, 0, math.radians(45))
    bpy.context.active_object.scale = (1.0, 0.78, 1.0)
    sphere("finial", 0.06, (0, 0, 2.86), "brass")
    blk("vane", (0.30, 0.02, 0.10), (0.06, 0, 2.98), "iron", metal=0.6)
    # The winch across the posts, the rope, and the bucket left on the rim.
    cyl("winch", 0.07, 0.86, (0, 0, 1.64), "oak", rot=(0, math.pi / 2, 0), verts=12)
    for k in range(4):
        cyl("rope_turn_%d" % k, 0.085, 0.05, (-0.12 + k * 0.07, 0, 1.64), "straw", rot=(0, math.pi / 2, 0), verts=12)
    blk("crank", (0.06, 0.06, 0.26), (0.52, -0.04, 1.54), "iron", metal=0.6)
    blk("crank_handle", (0.14, 0.05, 0.05), (0.58, -0.04, 1.42), "oak_light")
    blk("rope_down", (0.025, 0.025, 0.60), (0.10, -0.06, 1.30), "straw", bev=0)
    cyl("bucket", 0.12, 0.20, (0.40, -0.44, 0.92), "oak_light", verts=12)
    cyl("bucket_hoop", 0.125, 0.03, (0.40, -0.44, 0.96), "iron", verts=12, metal=0.6)
    # A board hung from the front beam.
    blk("notice", (0.46, 0.04, 0.24), (0, -0.40, 1.88), "paper")
    blk("notice_rope", (0.30, 0.02, 0.02), (0, -0.40, 2.02), "straw", bev=0)
    return 3.3


bp.PALETTE.update({
    "lamp_iron":  (0.180, 0.190, 0.210),
})


def prop_street_lamp():
    """Havenbrook's street lamp: a black iron post on a stone foot, a
    scrolled arm, and a four-paned lantern, lit. Thicker than life: at
    ninety pixels a real lamp post is a hairline."""
    blk("foot", (0.36, 0.36, 0.18), (0, 0, 0.09), "stone_pale")
    cyl("base", 0.13, 0.34, (0, 0, 0.35), "lamp_iron", verts=10, metal=0.5)
    cyl("post", 0.065, 1.70, (0, 0, 1.30), "lamp_iron", verts=10, metal=0.5)
    for k in range(3):
        cyl("collar_%d" % k, 0.09, 0.06, (0, 0, 0.56 + k * 0.66), "lamp_iron", verts=10, metal=0.5)
    blk("lantern_floor", (0.36, 0.36, 0.05), (0, 0, 2.18), "lamp_iron", metal=0.5)
    blk("lantern_glass", (0.30, 0.30, 0.38), (0, 0, 2.40), "candle_glow", emit=1.5, bev=0.01)
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("lantern_rib_%d_%d" % (sx, sy), (0.045, 0.045, 0.40), (sx * 0.15, sy * 0.15, 2.40), "lamp_iron",
                metal=0.5, bev=0)
    cap = cone("lantern_cap", 0.30, 0.22, (0, 0, 2.70), "lamp_iron", verts=4)
    cap.rotation_euler = (0, 0, math.radians(45))
    sphere("lantern_top", 0.05, (0, 0, 2.84), "lamp_iron")
    for sx in (-1, 1):
        blk("scroll_%d" % sx, (0.26, 0.04, 0.04), (sx * 0.12, 0, 2.06), "lamp_iron", rot=(0, sx * 0.6, 0), bev=0)
    return 3.2


def prop_planter():
    """A stone trough of flowers for the square's edge."""
    rng = random.Random(8)
    blk("trough", (1.10, 0.46, 0.40), (0, 0, 0.20), "stone_pale")
    blk("lip", (1.18, 0.54, 0.06), (0, 0, 0.42), "stone")
    blk("soil", (1.00, 0.36, 0.04), (0, 0, 0.44), "earth_dk", bev=0)
    tones = ("geranium", "marigold_f", "petal_pink", "petal_white", "geranium")
    for k in range(13):
        x = -0.44 + k * 0.074
        y = rng.uniform(-0.12, 0.12)
        h = rng.uniform(0.10, 0.22)
        sphere("leaf_%d" % k, 0.09, (x, y, 0.50 + h * 0.4), "leaf")
        sphere("bloom_%d" % k, 0.055, (x, y - 0.06, 0.52 + h), tones[k % len(tones)])
    return 1.5


def prop_park_bench():
    """A slatted bench on iron ends, for the square."""
    for k in range(3):
        blk("seat_%d" % k, (1.30, 0.10, 0.05), (0, -0.14 + k * 0.12, 0.46), "oak_light")
    for k in range(2):
        blk("back_%d" % k, (1.30, 0.05, 0.10), (0, 0.22, 0.66 + k * 0.16), "oak_light", rot=(math.radians(-12), 0, 0))
    for sx in (-1, 1):
        x = sx * 0.56
        blk("leg_f_%d" % sx, (0.06, 0.06, 0.46), (x, -0.18, 0.23), "lamp_iron", metal=0.5)
        blk("leg_b_%d" % sx, (0.06, 0.06, 0.86), (x, 0.24, 0.43), "lamp_iron", metal=0.5)
        blk("arm_%d" % sx, (0.06, 0.46, 0.05), (x, 0.02, 0.64), "lamp_iron", metal=0.5)
        blk("foot_%d" % sx, (0.08, 0.50, 0.04), (x, 0.03, 0.02), "lamp_iron", metal=0.5)
    return 1.8


# --- inside: the Barley and Bell, and the mayor's hall ------------------------------

bp.PALETTE.update({
    "lead":          (0.300, 0.300, 0.320),
    "pane":          (0.690, 0.800, 0.860),
    "pane_lit":      (0.960, 0.880, 0.640),
    "armchair_red":  (0.560, 0.170, 0.150),
    "armchair_dk":   (0.400, 0.110, 0.100),
    "parchment":     (0.880, 0.820, 0.650),
    "ink_road":      (0.520, 0.400, 0.300),
    "ink_blue":      (0.330, 0.470, 0.640),
    "ink_green":     (0.460, 0.600, 0.360),
    "felt_green":    (0.200, 0.380, 0.260),
})


def prop_inn_window():
    """A leaded window for a back wall, in a timber frame with a sill and a
    pot of herbs: light in a room that had four walls of plaster."""
    blk("frame", (0.96, 0.10, 1.08), (0, 0.04, 0.96), "oak")
    blk("glass", (0.80, 0.04, 0.92), (0, -0.01, 0.96), "pane", rough=0.25, bev=0)
    # The lead cames in a grid of small panes, inside the glass.
    for k in range(1, 4):
        blk("came_v_%d" % k, (0.025, 0.02, 0.90), (-0.40 + k * 0.20, -0.035, 0.96), "lead", bev=0)
    for k in range(1, 5):
        blk("came_h_%d" % k, (0.78, 0.02, 0.025), (0, -0.035, 0.50 + k * 0.184), "lead", bev=0)
    for sx in (-1, 1):
        blk("jamb_%d" % sx, (0.14, 0.12, 1.10), (sx * 0.47, -0.02, 0.96), "oak")
    blk("head", (1.02, 0.12, 0.14), (0, -0.02, 1.52), "oak")
    blk("mullion", (0.06, 0.08, 0.92), (0, -0.04, 0.96), "oak")
    blk("transom", (0.84, 0.08, 0.06), (0, -0.04, 1.14), "oak")
    blk("sill", (1.10, 0.24, 0.07), (0, -0.08, 0.40), "oak_light")
    blk("under", (1.02, 0.10, 0.40), (0, 0.04, 0.20), "oak", bev=0)
    cyl("pot", 0.10, 0.14, (0.28, -0.10, 0.51), "clay", verts=12)
    for k in range(5):
        a = k / 5 * math.tau
        sphere("herb_%d" % k, 0.06, (0.28 + math.cos(a) * 0.05, -0.10 + math.sin(a) * 0.04, 0.62), "leaf")
    return 2.0


def prop_barrel_table():
    """An upright cask with a board on it: somewhere to stand and drink."""
    cyl("cask", 0.30, 0.78, (0, 0, 0.39), "oak", verts=18)
    cyl("belly", 0.325, 0.40, (0, 0, 0.39), "oak_light", verts=18)
    for z in (0.14, 0.64):
        cyl("hoop_%.2f" % z, 0.315, 0.05, (0, 0, z), "iron", verts=18, metal=0.7)
    cyl("board", 0.44, 0.06, (0, 0, 0.81), "oak_pale", verts=20)
    bp.tankard("tk_a", -0.16, -0.08, 0.84)
    bp.tankard("tk_b", 0.18, 0.06, 0.84, full=False)
    for k in range(2):
        blk("die_%d" % k, (0.06, 0.06, 0.06), (0.02 + k * 0.09, -0.20, 0.87), "petal_white", bev=0.012)
    return 1.3


def prop_armchair():
    """A wing-backed armchair by the fire, red leather gone brown at the arms."""
    blk("seat", (0.74, 0.66, 0.16), (0, 0, 0.42), "armchair_red", bev=0.05)
    blk("cushion", (0.58, 0.54, 0.10), (0, -0.02, 0.54), "armchair_red", bev=0.05)
    blk("back", (0.74, 0.18, 0.80), (0, 0.28, 0.82), "armchair_red", bev=0.06)
    for sx in (-1, 1):
        blk("arm_%d" % sx, (0.14, 0.62, 0.26), (sx * 0.34, 0.0, 0.62), "armchair_dk", bev=0.05)
        blk("wing_%d" % sx, (0.10, 0.26, 0.46), (sx * 0.34, 0.22, 0.98), "armchair_red", bev=0.04)
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.08, 0.08, 0.34), (sx * 0.30, sy * 0.26, 0.17), "oak")
    return 1.9


def prop_stage():
    """The bard's corner: a low stage of planks with a lip, a stool, a lute
    against it, a drum, and candles along the front for footlights."""
    W, D, H = 2.20, 1.10, 0.24
    blk("deck", (W, D, H), (0, 0, H / 2), "oak_light")
    for k in range(7):
        blk("plank_%d" % k, (0.02, D, 0.01), (-W / 2 + 0.31 * (k + 0.5), 0, H + 0.004), "oak", bev=0)
    blk("lip", (W + 0.06, 0.08, 0.10), (0, -D / 2, H - 0.02), "oak")
    blk("step", (0.50, 0.26, 0.12), (0.72, -D / 2 - 0.13, 0.06), "oak")
    # A stool, a lute leaning on it.
    cyl("stool", 0.18, 0.06, (-0.20, 0.10, H + 0.46), "oak")
    for i in range(3):
        a = math.radians(90 + i * 120)
        blk("stool_leg_%d" % i, (0.05, 0.05, 0.44), (-0.20 + math.cos(a) * 0.11, 0.10 + math.sin(a) * 0.11, H + 0.22),
            "oak")
    lute = bp.sphere("lute_body", 0.20, (0.14, 0.02, H + 0.30), "oak_pale")
    lute.scale = (1.0, 0.45, 1.25)
    blk("lute_hole", (0.07, 0.02, 0.07), (0.14, -0.08, H + 0.34), "void", bev=0)
    blk("lute_neck", (0.06, 0.04, 0.46), (0.22, 0.06, H + 0.66), "oak", rot=(0, math.radians(-14), 0))
    blk("lute_head", (0.08, 0.05, 0.12), (0.28, 0.08, H + 0.92), "oak", rot=(0, math.radians(-40), 0))
    # A drum.
    cyl("drum", 0.18, 0.26, (0.70, 0.18, H + 0.13), "armchair_red", verts=16)
    cyl("drum_skin", 0.18, 0.02, (0.70, 0.18, H + 0.27), "paper", verts=16)
    # Footlights.
    for k in range(5):
        x = -0.84 + k * 0.42
        cyl("fl_%d" % k, 0.04, 0.10, (x, -D / 2 + 0.12, H + 0.05), "cloth_cream", verts=8)
        sphere("fl_flame_%d" % k, 0.035, (x, -D / 2 + 0.12, H + 0.13), "ember", emit=1.8)
    return 2.7


def prop_town_map():
    """A map of Havenbrook for the mayor's wall: parchment in a gilt frame,
    the two streets crossing, the square, the pond, and the fence round it."""
    blk("frame", (1.30, 0.06, 0.96), (0, 0.04, 0.96), "brass", metal=0.5)
    blk("sheet", (1.18, 0.04, 0.84), (0, 0.00, 0.96), "parchment", bev=0)
    z0 = 0.96
    blk("road_ew", (1.10, 0.02, 0.07), (0, -0.03, z0), "ink_road", bev=0)
    blk("road_ns", (0.07, 0.02, 0.44), (0, -0.03, z0 - 0.18), "ink_road", bev=0)
    blk("square", (0.30, 0.02, 0.20), (0, -0.03, z0), "ink_road", bev=0)
    blk("fence", (1.06, 0.015, 0.76), (0, -0.025, z0), "ink_green", bev=0)
    blk("fence_in", (1.00, 0.02, 0.70), (0, -0.028, z0), "parchment", bev=0)
    blk("road_ew2", (1.02, 0.02, 0.07), (0, -0.035, z0), "ink_road", bev=0)
    blk("road_ns2", (0.07, 0.02, 0.34), (0, -0.035, z0 - 0.18), "ink_road", bev=0)
    blk("square2", (0.30, 0.02, 0.20), (0, -0.035, z0), "ink_road", bev=0)
    cyl("pond", 0.12, 0.02, (0.30, -0.035, z0 - 0.22), "ink_blue", rot=(math.pi / 2, 0, 0), verts=16)
    for k, (x, z) in enumerate(((-0.30, 0.12), (-0.18, 0.12), (0.20, 0.12), (0.34, 0.12), (-0.14, -0.16), (0.14, -0.16),
                                 (0.0, 0.20))):
        blk("house_%d" % k, (0.08, 0.02, 0.07), (x, -0.04, z0 + z), "armchair_red", bev=0)
    for sx in (-1, 1):
        blk("tassel_%d" % sx, (0.04, 0.03, 0.12), (sx * 0.58, -0.02, z0 - 0.54), "flag_gold")
    return 1.6


# --- Mossvale: the brick wall ---------------------------------------------------------

bp.PALETTE.update({
    "mv_brick":      (0.560, 0.330, 0.250),
    "mv_brick_lt":   (0.640, 0.400, 0.300),
    "mv_brick_dk":   (0.440, 0.260, 0.200),
    "mv_coping":     (0.580, 0.580, 0.540),
    "mv_moss":       (0.380, 0.500, 0.250),
})


def _wall_block(name, x, y, w, d, h, rng, face=True):
    """A length of Mossvale's wall: brick in stretcher bond on the face the
    camera sees, a stone coping on top with moss in its joints."""
    blk(name + "_core", (w, d, h), (x, y, h / 2), "mv_brick_dk", bev=0)
    if face:
        _bricks(name + "_face", x - w / 2, x + w / 2, 0.0, h, y - d / 2 - 0.01,
                ("mv_brick", "mv_brick_lt", "mv_brick", "mv_brick_dk"), rng, course=0.12, brick=0.26)
    blk(name + "_coping", (w + 0.06, d + 0.10, 0.10), (x, y, h + 0.05), "mv_coping", bev=0.02)
    for k in range(int(w / 0.3)):
        if rng.random() < 0.55:
            sphere("%s_moss_%d" % (name, k), rng.uniform(0.04, 0.07),
                   (x - w / 2 + 0.15 + k * 0.3 + rng.uniform(-0.05, 0.05), y + rng.uniform(-0.1, 0.1), h + 0.11),
                   "mv_moss")
    # Moss low down where the wall meets the ground, on the damp side.
    for k in range(int(w / 0.4)):
        if rng.random() < 0.5:
            sphere("%s_foot_%d" % (name, k), rng.uniform(0.05, 0.08),
                   (x - w / 2 + 0.2 + k * 0.4, y - d / 2 - 0.02, 0.04), "mv_moss")


def prop_brick_wall():
    """One length of the brick wall round Mossvale, laid a length at a time
    along the north and south sides, the way the palisade was."""
    _wall_block("wall", 0, 0, 1.72, 0.36, 1.05, random.Random(21))
    return 2.0


def prop_brick_wall_side():
    """The wall down the east and west sides, seen end-on: a short piece,
    stacked down the side every few pixels so the pieces make a run."""
    _wall_block("wall", 0, 0, 0.36, 0.70, 1.05, random.Random(22))
    return 2.0


def prop_brick_pier():
    """A gate pier: a square brick column with a stone cap, a ball on top,
    and a lantern hung from it -- one either side of the west gate."""
    rng = random.Random(23)
    blk("core", (0.62, 0.62, 1.70), (0, 0, 0.85), "mv_brick_dk", bev=0)
    _bricks("face", -0.31, 0.31, 0.0, 1.70, -0.32, ("mv_brick", "mv_brick_lt", "mv_brick", "mv_brick_dk"), rng,
            course=0.12, brick=0.26)
    blk("plinth", (0.74, 0.74, 0.20), (0, 0, 0.10), "mv_coping")
    blk("cap", (0.78, 0.78, 0.12), (0, 0, 1.76), "mv_coping", bev=0.02)
    blk("cap2", (0.62, 0.62, 0.10), (0, 0, 1.87), "mv_coping", bev=0.02)
    sphere("ball", 0.20, (0, 0, 2.10), "mv_coping")
    for k in range(4):
        sphere("moss_%d" % k, 0.07, (-0.25 + k * 0.17, -0.28, 1.84), "mv_moss")
    blk("bracket", (0.06, 0.34, 0.06), (0, -0.46, 1.36), "iron", metal=0.6)
    blk("lantern", (0.18, 0.18, 0.24), (0, -0.60, 1.18), "candle_glow", emit=1.4)
    blk("lantern_cap", (0.22, 0.22, 0.06), (0, -0.60, 1.33), "iron", metal=0.6)
    return 2.6


# --- the dwarves' mine under Mossvale ----------------------------------------------------

bp.PALETTE.update({
    "dw_stone":      (0.520, 0.500, 0.470),
    "dw_stone_lt":   (0.640, 0.620, 0.580),
    "dw_stone_dk":   (0.380, 0.360, 0.340),
    "dw_rock":       (0.440, 0.380, 0.330),
    "dw_rock_dk":    (0.320, 0.270, 0.230),
    "dw_gold":       (0.860, 0.660, 0.240),
    "dw_rune":       (0.460, 0.760, 0.900),
})


def _rock(name, x, y, z, r, rng, tone=None):
    ob = sphere(name, r, (x, y, z), tone or ("dw_rock", "dw_rock_dk", "rock")[rng.randrange(3)])
    ob.scale = (rng.uniform(0.9, 1.3), rng.uniform(0.8, 1.1), rng.uniform(0.6, 0.9))
    return ob


def prop_dwarf_mine_gate():
    """The way down to the dwarves: a door cut into a knoll of rock, framed in
    dressed stone the way only they dress it -- square jambs cut with runes
    that glow a little, a stepped lintel with a bearded face over it, an
    iron-bound door stood open on the dark, and rails coming out of it.

    Nothing like the Emberfell adit, which is a hole with a beam over it:
    this is somebody's front door."""
    rng = random.Random(31)
    # The knoll.
    for k in range(34):
        a = rng.uniform(0, math.pi)
        r = rng.uniform(0.6, 1.0)
        x = math.cos(a) * 2.3 * r
        z = math.sin(a) * 1.9 * r
        _rock("knoll_%d" % k, x, rng.uniform(0.1, 0.9), z, rng.uniform(0.35, 0.62), rng)
    for k in range(10):
        sphere("moss_%d" % k, rng.uniform(0.12, 0.2), (rng.uniform(-2.0, 2.0), rng.uniform(0.0, 0.6), rng.uniform(1.2, 2.0)),
               "mv_moss")
    # The portal: two jambs, a stepped lintel, the face.
    fy = -0.62
    for sx in (-1, 1):
        blk("jamb_%d" % sx, (0.44, 0.50, 1.70), (sx * 0.76, fy + 0.10, 0.85), "dw_stone")
        blk("jamb_cap_%d" % sx, (0.54, 0.56, 0.14), (sx * 0.76, fy + 0.10, 1.76), "dw_stone_lt")
        for k in range(4):
            blk("rune_%d_%d" % (sx, k), (0.12, 0.03, 0.14), (sx * 0.76, fy - 0.16, 0.40 + k * 0.32), "dw_rune",
                emit=0.9, bev=0)
    blk("lintel", (2.10, 0.54, 0.34), (0, fy + 0.10, 1.96), "dw_stone")
    blk("lintel_step", (1.60, 0.54, 0.26), (0, fy + 0.10, 2.26), "dw_stone_lt")
    blk("lintel_top", (1.00, 0.54, 0.22), (0, fy + 0.10, 2.50), "dw_stone")
    # A face in the lintel: brow, nose, and a beard of stone down to the door.
    blk("face_brow", (0.54, 0.06, 0.10), (0, fy - 0.19, 2.34), "dw_stone_dk")
    blk("face_nose", (0.12, 0.08, 0.18), (0, fy - 0.20, 2.20), "dw_stone_lt")
    for k in range(5):
        blk("face_beard_%d" % k, (0.10, 0.06, 0.28), (-0.24 + k * 0.12, fy - 0.19, 1.98 - (abs(k - 2) * 0.04)),
            "dw_stone_dk", bev=0.02)
    for sx in (-1, 1):
        sphere("face_eye_%d" % sx, 0.045, (sx * 0.14, fy - 0.20, 2.28), "dw_gold", emit=0.6)
    # The dark doorway, and the door stood open against the left jamb.
    blk("dark", (1.08, 0.40, 1.60), (0, fy + 0.30, 0.80), "void", bev=0)
    blk("door", (0.12, 0.62, 1.54), (-0.44, fy - 0.20, 0.79), "oak", rot=(0, 0, math.radians(-58)))
    for zz in (0.35, 0.80, 1.25):
        blk("door_band_%.2f" % zz, (0.14, 0.64, 0.06), (-0.44, fy - 0.20, zz), "iron", rot=(0, 0, math.radians(-58)),
            metal=0.7)
    # Rails out of the door and down the step, and a cart waiting on them.
    for sx in (-1, 1):
        blk("rail_%d" % sx, (0.05, 1.60, 0.05), (sx * 0.26, fy - 0.40, 0.04), "iron", metal=0.7)
    for k in range(6):
        blk("sleeper_%d" % k, (0.76, 0.12, 0.04), (0, fy + 0.30 - k * 0.28, 0.02), "oak")
    # Lanterns on posts either side.
    for sx in (-1, 1):
        x = sx * 1.34
        blk("lamp_post_%d" % sx, (0.10, 0.10, 1.30), (x, fy - 0.40, 0.65), "oak")
        blk("lamp_%d" % sx, (0.20, 0.20, 0.26), (x, fy - 0.40, 1.40), "candle_glow", emit=1.4)
        blk("lamp_cap_%d" % sx, (0.26, 0.26, 0.06), (x, fy - 0.40, 1.56), "iron", metal=0.6)
    # A pick and a shovel leant on the right jamb, and a barrel.
    blk("pick_haft", (0.05, 0.05, 0.90), (1.10, fy - 0.26, 0.45), "oak", rot=(0, math.radians(12), 0))
    blk("pick_head", (0.40, 0.06, 0.06), (1.20, fy - 0.26, 0.90), "iron", metal=0.6, rot=(0, math.radians(12), 0))
    cyl("barrel", 0.20, 0.46, (-1.30, fy - 0.10, 0.23), "oak", verts=12)
    cyl("barrel_hoop", 0.21, 0.04, (-1.30, fy - 0.10, 0.36), "iron", verts=12, metal=0.6)
    return (5.6, 40.0)


def prop_mine_support():
    """Pit props: two timber posts and a cap beam, braced, against a
    tunnel's back wall -- what keeps the hill off the dwarves."""
    for sx in (-1, 1):
        blk("post_%d" % sx, (0.20, 0.20, 1.70), (sx * 0.70, 0, 0.85), "log")
        blk("brace_%d" % sx, (0.12, 0.12, 0.62), (sx * 0.52, 0, 1.52), "log_dk", rot=(0, sx * math.radians(45), 0))
    blk("cap", (1.80, 0.26, 0.22), (0, 0, 1.78), "log")
    blk("wedge", (0.20, 0.28, 0.08), (0.30, 0, 1.93), "oak_light")
    return 2.5


def prop_mine_lantern():
    """A miner's lantern on an iron stand."""
    blk("foot", (0.30, 0.30, 0.06), (0, 0, 0.03), "iron", metal=0.6)
    blk("pole", (0.05, 0.05, 1.20), (0, 0, 0.62), "iron", metal=0.6)
    blk("arm", (0.30, 0.05, 0.05), (0.13, 0, 1.20), "iron", metal=0.6)
    blk("lamp", (0.18, 0.18, 0.24), (0.26, 0, 1.02), "candle_glow", emit=1.5)
    blk("lamp_cap", (0.22, 0.22, 0.06), (0.26, 0, 1.17), "iron", metal=0.6)
    return 1.6


def prop_dwarf_statue():
    """A king of the dwarves in stone, leaning on his axe, for the hall."""
    blk("plinth", (1.00, 0.80, 0.30), (0, 0, 0.15), "dw_stone_dk")
    blk("plinth_top", (0.90, 0.70, 0.10), (0, 0, 0.35), "dw_stone")
    for sx in (-1, 1):
        blk("leg_%d" % sx, (0.20, 0.24, 0.40), (sx * 0.14, 0, 0.60), "dw_stone")
        blk("boot_%d" % sx, (0.24, 0.32, 0.12), (sx * 0.14, -0.04, 0.46), "dw_stone_dk")
    blk("body", (0.66, 0.44, 0.60), (0, 0, 1.10), "dw_stone", bev=0.08)
    blk("belt", (0.68, 0.46, 0.10), (0, 0, 0.86), "dw_stone_dk")
    sphere("head", 0.22, (0, -0.02, 1.58), "dw_stone")
    beard = sphere("beard", 0.22, (0, -0.16, 1.26), "dw_stone_lt")
    beard.scale = (1.0, 0.6, 1.5)
    blk("helm", (0.46, 0.44, 0.12), (0, 0, 1.74), "dw_stone_dk")
    cone("helm_top", 0.22, 0.20, (0, 0, 1.89), "dw_stone_dk", verts=8)
    for sx in (-1, 1):
        blk("arm_%d" % sx, (0.16, 0.18, 0.50), (sx * 0.40, -0.04, 1.08), "dw_stone")
    # The axe, head down on the plinth in front of him.
    blk("haft", (0.07, 0.07, 1.10), (0, -0.34, 0.95), "dw_stone_dk")
    blk("axe_head", (0.52, 0.08, 0.30), (0, -0.36, 0.55), "dw_stone_lt")
    sphere("gem", 0.05, (0, -0.42, 1.30), "dw_gold", emit=0.5)
    return 2.4


# --- Fernhollow: the hamlet on the water ----------------------------------------------------

bp.PALETTE.update({
    "fh_wood":       (0.560, 0.500, 0.420),
    "fh_wood_dk":    (0.400, 0.350, 0.290),
    "fh_wood_lt":    (0.660, 0.600, 0.510),
    "fh_trim":       (0.290, 0.500, 0.520),
    "fh_trim_dk":    (0.200, 0.360, 0.380),
    "fh_limewash":   (0.890, 0.880, 0.830),
    "net":           (0.300, 0.300, 0.260),
    "float_red":     (0.780, 0.300, 0.220),
    "fish_silver":   (0.740, 0.780, 0.800),
})


bp.PALETTE.update({
    # Reed thatch by the water: greyer and duller than a straw roof, so the
    # hamlet's roofs are not a row of yellow boards.
    "reed_th":       (0.580, 0.520, 0.380),
    "reed_th_dk":    (0.440, 0.390, 0.280),
    "reed_th_lt":    (0.660, 0.600, 0.440),
})


def _thatch(W, D, eave, rng, pitch_deg=42, over=0.26):
    """Reed thatch. Laid reed runs down the roof, so its texture is streaks
    down the slope: strips from the eave up, of uneven length and tone, over
    a darker slab, with a thick rolled eave and a fringe of reed ends under
    it. Bars across the slope read as planks, and short blocks as stone
    tiles -- both were tried. Returns the rise."""
    pitch = math.radians(pitch_deg)
    half = D / 2 + over
    slab = half / math.cos(pitch)
    rise = half * math.tan(pitch)
    for side in (-1, 1):
        blk("thatch_%d" % side, (W + over * 2, slab, 0.20), (0, side * half / 2, eave + rise / 2),
            "reed_th_dk", rot=(-side * pitch, 0, 0), bev=0.08)
    # The front slope's streaks. A strip of length L from the eave has its
    # middle L/2 up the slope, and stands 0.11 proud of the slab's face.
    ny, nz = -math.sin(pitch), math.cos(pitch)
    tones = ("reed_th", "reed_th_lt", "reed_th", "reed_th_dk", "reed_th_lt")
    x = -W / 2 - over + 0.03
    k = 0
    while x < W / 2 + over - 0.03:
        for layer in range(2):
            L = slab * (rng.uniform(0.85, 1.0) if layer == 0 else rng.uniform(0.25, 0.65))
            start = 0.0 if layer == 0 else rng.uniform(0.0, slab - L)
            mid = start + L / 2
            y = -half + mid * math.cos(pitch) + ny * (0.11 + layer * 0.02)
            z = eave + mid * math.sin(pitch) + nz * (0.11 + layer * 0.02)
            blk("reed_%d_%d" % (k, layer), (0.065, L, 0.05), (x + rng.uniform(-0.01, 0.01), y, z),
                tones[rng.randrange(len(tones))], rot=(pitch, 0, 0), bev=0.015)
        x += 0.07
        k += 1
    cyl("eave_roll", 0.13, W + over * 2 + 0.04, (0, -half + 0.03, eave), "reed_th",
        rot=(0, math.radians(90), 0), verts=16)
    n = int((W + over * 2) / 0.08)
    for i in range(n):
        xx = -W / 2 - over + (i + 0.5) * (W + over * 2) / n
        blk("fringe_%d" % i, (0.07, 0.09, 0.08 + rng.random() * 0.08), (xx, -half - 0.05, eave - 0.14),
            ("reed_th_dk", "reed_th")[i % 2], bev=0.02)
    cyl("ridge", 0.15, W + over * 2 + 0.06, (0, 0, eave + rise + 0.08), "reed_th_dk",
        rot=(0, math.radians(90), 0), verts=16)
    return rise


def _boards(name, x0, x1, z0, z1, y, rng, tones=("fh_wood", "fh_wood_lt", "fh_wood_dk", "fh_wood"), gap=None):
    """Upright weatherboards on a face."""
    x = x0
    k = 0
    while x < x1 - 0.02:
        w = min(0.20 + rng.random() * 0.06, x1 - x)
        cx = x + w / 2
        if not (gap and gap(cx)):
            blk("%s_%d" % (name, k), (w - 0.02, 0.06, z1 - z0), (cx, y, (z0 + z1) / 2), tones[rng.randrange(len(tones))],
                bev=0.006)
        x += w
        k += 1


def _broken_window(name, x, y, z, w, h, rng, snapped):
    """A window somebody came in through: the glass gone but for jagged pieces
    caught in the corners, the dark room behind, and the bars snapped."""
    blk(name + "_frame", (w + 0.10, 0.06, h + 0.10), (x, y, z), "oak")
    blk(name + "_dark", (w, 0.04, h), (x, y - 0.03, z), "void", bev=0)
    for k, (cx, cz, a) in enumerate(((-1, 1, 30), (1, 1, -40), (-1, -1, 62), (1, -1, -18), (0, 1, 8))):
        if k == 4 and rng.random() < 0.5:
            continue
        sx = x + cx * (w / 2 - 0.07)
        sz = z + cz * (h / 2 - 0.08)
        blk("%s_shard_%d" % (name, k), (0.07 + rng.random() * 0.05, 0.02, 0.11 + rng.random() * 0.07),
            (sx, y - 0.055, sz), "glass", rot=(0, math.radians(a), 0), bev=0)
    if snapped:
        # The upright snapped and hanging; the cross-bar gone.
        blk(name + "_mullion", (0.04, 0.05, h * 0.55), (x + 0.04, y - 0.06, z + h * 0.20), "oak",
            rot=(0, math.radians(20), 0), bev=0)
    else:
        blk(name + "_mullion", (0.04, 0.05, h), (x, y - 0.05, z), "oak", bev=0)
        blk(name + "_transom", (w * 0.55, 0.05, 0.04), (x - w * 0.22, y - 0.05, z + h * 0.1), "oak",
            rot=(0, math.radians(-14), 0), bev=0)
    blk(name + "_sill", (w + 0.16, 0.14, 0.05), (x, y - 0.06, z - h / 2 - 0.05), "stone_pale")


def _scratches(name, x, y, z, n, length=0.32, lean=-22):
    """Long ragged scratches, a few side by side: claws, not a blade."""
    for k in range(n):
        blk("%s_%d" % (name, k), (0.014, 0.012, length * (0.8 + 0.1 * k)), (x + k * 0.05, y, z - k * 0.02),
            "wood_cut", rot=(0, math.radians(lean), 0), bev=0)


def _fisher_cottage(variant, wreck=0):
    """A fisher's cottage at Fernhollow: low, reed-thatched, on a stone footing
    against the damp, with the day's work hung on it -- a net on the wall, an
    oar by the door, fish drying under the eave. One is grey weatherboard with
    a blue door; the other limewashed with blue shutters.

    Mara's is the first, after the night she was taken (scenes 58-59): `wreck`
    1 is how it is found -- both windows smashed in, a shutter hanging by one
    hinge and the other torn off and thrown down, the door split and hanging
    crooked in its frame, claw marks on it, the oar knocked down and the
    bucket on its side by the step; 2 is the same once the door has fallen in
    at a knock, the doorway left dark and open."""
    rng = random.Random(51 + variant)
    W, D, H = 2.40, 1.20, 1.55
    front = -D / 2
    base = 0.18
    blk("footing", (W + 0.14, D + 0.14, base), (0, 0, base / 2), "stone")
    for k in range(7):
        blk("foot_stone_%d" % k, (0.34, 0.06, base - 0.03), (-W / 2 + 0.18 + k * 0.345, front - 0.08, base / 2),
            ("stone", "stone_pale")[k % 2], bev=0.02)
    blk("walls", (W, D, H), (0, 0, base + H / 2), "fh_wood_dk" if variant == 0 else "fh_limewash", bev=0.03)
    door_gap = lambda cx: abs(cx) < 0.36
    if variant == 0:
        _boards("board", -W / 2, W / 2, base, base + H, front - 0.03, rng, gap=door_gap)
    for side in (-1, 1):
        blk("corner_%d" % side, (0.10, 0.10, H), (side * (W / 2 - 0.02), front - 0.05, base + H / 2), "fh_wood_dk")
    if wreck == 0:
        blk("door", (0.62, 0.08, 0.96), (0, front - 0.05, base + 0.48), "fh_trim")
        for i in range(2):
            blk("door_plank_%d" % i, (0.02, 0.02, 0.92), (-0.1 + i * 0.2, front - 0.10, base + 0.48), "fh_trim_dk", bev=0)
        sphere("door_knob", 0.04, (0.20, front - 0.11, base + 0.48), "brass")
    else:
        # The dark of the house behind the doorway.
        blk("doorway", (0.62, 0.03, 0.96), (0, front - 0.015, base + 0.48), "void", bev=0)
        if wreck == 1:
            # Split, and hanging crooked off its top hinge: a dark wedge of the
            # doorway showing down one side, a panel stove in, claw marks.
            tilt = (0, math.radians(-9), 0)
            blk("door", (0.58, 0.07, 0.92), (0.05, front - 0.07, base + 0.45), "fh_trim", rot=tilt)
            for i in range(2):
                blk("door_plank_%d" % i, (0.02, 0.02, 0.88), (-0.06 + i * 0.2, front - 0.115, base + 0.45),
                    "fh_trim_dk", rot=tilt, bev=0)
            blk("door_hole", (0.17, 0.03, 0.22), (0.14, front - 0.12, base + 0.66), "void", rot=tilt, bev=0)
            for k, (dx, dz, a) in enumerate(((-0.10, 0.08, 40), (0.10, 0.10, -35), (-0.08, -0.12, -50), (0.09, -0.11, 30))):
                blk("splinter_%d" % k, (0.02, 0.02, 0.09), (0.14 + dx, front - 0.125, base + 0.66 + dz), "wood_cut",
                    rot=(0, math.radians(a), 0), bev=0)
            _scratches("door_claw", -0.16, front - 0.12, base + 0.42, 3)
        else:
            # Fallen in at a knock: only the broken ends of it left on its hinges.
            for k, dz in enumerate((0.78, 0.22)):
                blk("hinge_stub_%d" % k, (0.07, 0.05, 0.10), (-0.28, front - 0.07, base + dz), "fh_trim", bev=0.005)
                blk("hinge_splinter_%d" % k, (0.02, 0.02, 0.07), (-0.23, front - 0.08, base + dz + 0.04), "wood_cut",
                    rot=(0, math.radians(-35), 0), bev=0)
    blk("door_lintel", (0.80, 0.10, 0.10), (0, front - 0.06, base + 1.00), "fh_wood_dk",
        rot=(0, math.radians(3 if wreck else 0), 0))
    blk("step", (0.86, 0.34, 0.08), (0, front - 0.24, 0.04), "stone_pale")
    for side in (-1, 1):
        x = side * 0.80
        if wreck:
            _broken_window("win_%d" % side, x, front - 0.05, base + 0.86, 0.40, 0.44, rng, snapped=(side > 0))
            continue
        window("win_%d" % side, x, front - 0.05, base + 0.86, w=0.40, h=0.44, lit=(side > 0), shutters=False)
        if variant == 1:
            for s2 in (-1, 1):
                blk("shutter_%d_%d" % (side, s2), (0.11, 0.05, 0.48), (x + s2 * 0.28, front - 0.07, base + 0.86),
                    "fh_trim")
    eave = base + H
    rise = _thatch(W, D, eave, rng)
    # The day's work: a net on the wall, fish under the eave, an oar.
    nx = -0.80 if variant == 0 else 0.80
    blk("net", (0.62, 0.03, 0.46), (nx, front - 0.10, base + 0.30), "net", bev=0)
    for k in range(3):
        sphere("float_%d" % k, 0.04, (nx - 0.24 + k * 0.24, front - 0.13, base + 0.52), "float_red")
    blk("line", (1.60, 0.02, 0.02), (0, front - 0.30, eave - 0.10), "straw", bev=0)
    for k in range(5):
        f = sphere("fish_%d" % k, 0.07, (-0.60 + k * 0.30, front - 0.30, eave - 0.24), "fish_silver")
        f.scale = (0.45, 0.35, 1.3)
    if wreck:
        # The shutters they came in through: one hanging by its top hinge,
        # swung askew and clawed; the other torn off and thrown down, flat on
        # the ground in front.
        blk("shutter_hung", (0.13, 0.05, 0.46), (-0.80 - 0.30, front - 0.09, base + 0.80), "fh_trim",
            rot=(0, math.radians(26), 0))
        _scratches("shutter_claw", -1.13, front - 0.12, base + 0.86, 2, length=0.22, lean=18)
        blk("shutter_split", (0.12, 0.05, 0.26), (0.80 + 0.29, front - 0.08, base + 0.97), "fh_trim",
            rot=(0, math.radians(-7), 0))
        blk("shutter_split_end", (0.03, 0.02, 0.08), (0.80 + 0.31, front - 0.10, base + 0.81), "wood_cut",
            rot=(0, math.radians(30), 0), bev=0)
        blk("shutter_down", (0.46, 0.13, 0.03), (-0.98, front - 0.27, 0.035), "fh_trim", rot=(0, 0, math.radians(24)))
        # The oar knocked down along the wall; the bucket on its side by the step.
        blk("oar", (1.10, 0.06, 0.06), (0.98, front - 0.17, 0.05), "oak_light", rot=(0, 0, math.radians(8)))
        blk("oar_blade", (0.34, 0.16, 0.04), (1.58, front - 0.12, 0.04), "oak_light", rot=(0, 0, math.radians(8)))
        cyl("bucket", 0.10, 0.16, (0.56, front - 0.22, 0.11), "oak_light", rot=(0, math.radians(90), math.radians(-24)),
            verts=12)
        cyl("bucket_mouth", 0.085, 0.02, (0.64, front - 0.26, 0.11), "void", rot=(0, math.radians(90), math.radians(-24)),
            verts=12)
        cyl("bucket_hoop", 0.105, 0.03, (0.52, front - 0.20, 0.11), "iron", rot=(0, math.radians(90), math.radians(-24)),
            verts=12, metal=0.7)
        # Glass under both windows, splinters on the step.
        for k in range(10):
            gx = (-0.80 if k % 2 else 0.80) + rng.uniform(-0.28, 0.28)
            blk("glass_down_%d" % k, (0.05, 0.04, 0.012), (gx, front - rng.uniform(0.10, 0.30), 0.012), "glass",
                rot=(0, 0, math.radians(rng.uniform(0, 180))), bev=0)
        for k in range(4):
            blk("splinter_down_%d" % k, (0.16, 0.03, 0.02), (rng.uniform(-0.30, 0.30), front - rng.uniform(0.14, 0.32), 0.09),
                "wood_cut" if k % 2 else "fh_trim", rot=(0, 0, math.radians(rng.uniform(0, 180))), bev=0)
    else:
        blk("oar", (0.06, 0.06, 1.10), (0.46 if variant == 0 else -0.46, front - 0.14, base + 0.50), "oak_light",
            rot=(0, math.radians(14), 0))
        blk("oar_blade", (0.16, 0.04, 0.34), (0.54 if variant == 0 else -0.38, front - 0.14, base + 1.02), "oak_light",
            rot=(0, math.radians(14), 0))
    # A stone chimney on the front slope.
    cx = -W / 2 + 0.50 if variant == 0 else W / 2 - 0.50
    blk("chimney", (0.32, 0.32, rise * 0.85), (cx, -0.18, eave + rise * 0.28 + rise * 0.42), "stone")
    blk("chimney_cap", (0.40, 0.40, 0.08), (cx, -0.18, eave + rise * 0.28 + rise * 0.85 + 0.04), "stone_pale")
    return (4.0, BUILDING_ELEVATION)


def prop_fisher_cottage_a():
    return _fisher_cottage(0)


def prop_fisher_cottage_b():
    return _fisher_cottage(1)


def prop_fisher_cottage_wrecked():
    """Mara's, as it is found (58): see _fisher_cottage."""
    return _fisher_cottage(0, wreck=1)


def prop_fisher_cottage_breached():
    """Mara's, once its door has fallen in (58): see _fisher_cottage."""
    return _fisher_cottage(0, wreck=2)


def prop_ferry_house():
    """The ferry house: the hamlet's biggest, on two floors -- stone below,
    weatherboard above in the blue-green of the boats -- under a deep thatch
    with a dormer, the ferry's bell on a post by the door and a board with
    an oar across it: FERRY. Where the ferryman lives, and the way into the
    ferry cottage's room."""
    rng = random.Random(60)
    W, D = 3.00, 1.40
    G, U = 1.25, 1.05
    front = -D / 2
    blk("plinth", (W + 0.12, D + 0.12, 0.12), (0, 0, 0.06), "stone_pale")
    blk("ground_core", (W - 0.04, D - 0.10, G), (0, 0.04, G / 2), "stone", bev=0)
    bp._coursed("gstone", -W / 2, W / 2, 0.12, G, front - 0.01, ("stone", "stone_pale", "rock"), rng,
                gap=lambda cx, z: (abs(cx) < 0.40 and z < 1.10) or
                any(abs(cx - wx) < 0.28 and 0.40 < z < 0.98 for wx in (-0.95, 0.95)))
    blk("door", (0.64, 0.08, 1.00), (0, front - 0.03, 0.12 + 0.50), "fh_trim")
    blk("door_frame", (0.80, 0.10, 0.12), (0, front - 0.04, 1.16), "fh_wood_dk")
    sphere("door_knob", 0.04, (0.20, front - 0.08, 0.62), "brass")
    blk("step", (0.90, 0.36, 0.10), (0, front - 0.22, 0.05), "stone_pale")
    for wx in (-0.95, 0.95):
        window("gwin_%.2f" % wx, wx, front - 0.03, 0.70, w=0.44, h=0.48, shutters=False)
    # The upper floor, jettied a little, in weatherboard.
    uy = front - 0.10
    zb = G + 0.10
    blk("floor_beam", (W + 0.20, D + 0.24, 0.10), (0, -0.06, G + 0.05), "fh_wood_dk")
    blk("upper_core", (W + 0.08, D + 0.10, U), (0, -0.05, zb + U / 2), "fh_trim_dk", bev=0.01)
    _boards("uboard", -W / 2 - 0.04, W / 2 + 0.04, zb, zb + U, uy - 0.02, rng,
            tones=("fh_trim", "fh_trim_dk", "fh_trim", "fh_trim"))
    for k, wx in enumerate((-0.90, 0.0, 0.90)):
        window("uwin_%d" % k, wx, uy - 0.06, zb + 0.54, w=0.38, h=0.42, shutters=False, lit=(k != 1))
    eave = zb + U
    rise = _thatch(W + 0.08, D + 0.10, eave, rng, pitch_deg=44)
    # A dormer in the thatch.
    half = (D + 0.10) / 2 + 0.26
    dz = eave + rise * 0.40
    dy = -half * 0.58
    blk("dormer", (0.60, 0.30, 0.50), (0, dy - 0.02, dz + 0.16), "fh_wood")
    window("dormer_win", 0, dy - 0.18, dz + 0.16, w=0.28, h=0.28, shutters=False)
    cone("dormer_roof", 0.46, 0.34, (0, dy - 0.02, dz + 0.56), "reed_th_dk", verts=4).rotation_euler = \
        (0, 0, math.radians(45))
    # The ferry board over the door, an oar across it.
    blk("board", (1.10, 0.06, 0.30), (0, uy - 0.10, zb + 0.10), "fh_limewash")
    blk("board_edge", (1.16, 0.04, 0.36), (0, uy - 0.07, zb + 0.10), "fh_wood_dk")
    for k in range(5):
        blk("letter_%d" % k, (0.08, 0.03, 0.16), (-0.36 + k * 0.18, uy - 0.14, zb + 0.10), "fh_trim_dk", bev=0)
    blk("oar_shaft", (1.40, 0.05, 0.05), (0, uy - 0.16, zb + 0.30), "oak_light", rot=(0, math.radians(-8), 0))
    blk("oar_blade", (0.30, 0.04, 0.14), (0.78, uy - 0.16, zb + 0.24), "oak_light", rot=(0, math.radians(-8), 0))
    # The bell on its post, to call the ferryman.
    blk("bell_post", (0.10, 0.10, 1.40), (1.30, front - 0.40, 0.70), "fh_wood_dk")
    blk("bell_arm", (0.34, 0.06, 0.06), (1.18, front - 0.40, 1.36), "fh_wood_dk")
    cone("bell", 0.10, 0.16, (1.06, front - 0.40, 1.22), "brass", verts=12)
    # Chimney.
    blk("chimney", (0.36, 0.36, rise * 0.9), (-W / 2 + 0.50, -0.20, eave + rise * 0.30 + rise * 0.45), "stone")
    blk("chimney_cap", (0.44, 0.44, 0.08), (-W / 2 + 0.50, -0.20, eave + rise * 1.24), "stone_pale")
    return (4.9, BUILDING_ELEVATION)


def prop_boat_shed():
    """A boat shed on the shore: an open front on posts, a plank floor, and
    a boat drawn up in it with its oars shipped."""
    rng = random.Random(70)
    W, D, H = 2.60, 1.30, 1.45
    front = -D / 2
    blk("floor", (W, D, 0.10), (0, 0, 0.05), "fh_wood")
    blk("back", (W, 0.10, H), (0, D / 2 - 0.05, H / 2), "fh_wood_dk")
    for side in (-1, 1):
        blk("side_%d" % side, (0.10, D, H), (side * (W / 2 - 0.05), 0, H / 2), "fh_wood_dk")
        blk("post_%d" % side, (0.14, 0.14, H), (side * (W / 2 - 0.07), front + 0.07, H / 2), "fh_wood_dk")
    blk("lintel", (W, 0.14, 0.16), (0, front + 0.07, H - 0.08), "fh_wood_dk")
    _boards("backb", -W / 2, W / 2, 0.10, H, D / 2 - 0.12, rng)
    rise = _thatch(W, D, H, rng, pitch_deg=36, over=0.20)
    # The boat, drawn up across the shed with its bow out of the door.
    hull = sphere("hull", 0.5, (0, -0.35, 0.30), "fh_trim")
    hull.scale = (1.9, 0.62, 0.44)
    inner = sphere("hull_in", 0.44, (0, -0.35, 0.38), "fh_wood_lt")
    inner.scale = (1.85, 0.52, 0.30)
    blk("thwart", (0.12, 0.50, 0.05), (0.10, -0.35, 0.42), "fh_wood_dk")
    for side in (-1, 1):
        blk("oar_%d" % side, (1.30, 0.05, 0.05), (0, -0.35 + side * 0.14, 0.46), "oak_light")
    for k in range(3):
        cyl("crate_%d" % k if k else "keg", 0.14, 0.30, (W / 2 - 0.40, 0.40 - k * 0.32, 0.25), "oak", verts=10)
    return (3.9, BUILDING_ELEVATION)


def prop_net_rack():
    """Two poles and a rail with a net hung out to dry over it, floats and all."""
    for sx in (-1, 1):
        blk("pole_%d" % sx, (0.08, 0.08, 1.30), (sx * 0.70, 0, 0.65), "fh_wood_dk")
    blk("rail", (1.60, 0.06, 0.06), (0, 0, 1.24), "fh_wood_dk")
    blk("net", (1.36, 0.03, 0.90), (0, -0.04, 0.80), "net", bev=0)
    for k in range(6):
        blk("mesh_h_%d" % k, (1.36, 0.02, 0.015), (0, -0.06, 0.40 + k * 0.16), "fh_wood_dk", bev=0)
    for k in range(5):
        sphere("float_%d" % k, 0.05, (-0.56 + k * 0.28, -0.07, 1.20), "float_red")
    return 2.0


def prop_post_lantern():
    """A tarred post with a lantern hung from an arm: Fernhollow's lamps."""
    blk("post", (0.12, 0.12, 1.60), (0, 0, 0.80), "fh_wood_dk")
    blk("arm", (0.40, 0.07, 0.07), (0.16, 0, 1.52), "fh_wood_dk")
    blk("hook", (0.02, 0.02, 0.10), (0.30, 0, 1.44), "iron", bev=0)
    blk("lamp", (0.18, 0.18, 0.24), (0.30, 0, 1.28), "candle_glow", emit=1.5)
    blk("lamp_cap", (0.22, 0.22, 0.06), (0.30, 0, 1.43), "iron", metal=0.6)
    return 2.0


def prop_wattle_fence():
    """A run of woven hazel between stakes, round a garden."""
    for k in range(5):
        blk("stake_%d" % k, (0.06, 0.06, 0.70), (-0.80 + k * 0.40, 0, 0.35), "twig")
    for r in range(5):
        z = 0.12 + r * 0.11
        for k in range(4):
            x = -0.60 + k * 0.40
            blk("weave_%d_%d" % (r, k), (0.44, 0.05, 0.08), (x, (0.02 if (r + k) % 2 else -0.02), z), "twig",
                bev=0.02)
    return 2.0


# --- a washing line ---------------------------------------------------------------------
#
# Two pictures laid one on the other in the same spot (make_props.ps1 sits them
# on the floor together, $ALIGN): the posts and the line, which stand still, and
# what is pegged out on it, which the wind takes (Shaders::ArtOf: "laundry_wash"
# is cloth hung from the top, so the hems move most and the line not at all).

bp.PALETTE.update({
    "linen":       (0.930, 0.910, 0.860),
    "linen_dk":    (0.820, 0.800, 0.750),
    "cloth_sky":   (0.600, 0.730, 0.860),
    "cloth_rose":  (0.870, 0.610, 0.610),
    "cloth_ochre": (0.800, 0.640, 0.360),
    "cloth_sage":  (0.600, 0.730, 0.560),
    "rope":        (0.780, 0.700, 0.540),
})

_LINE_X = 1.15
_LINE_TOP = 1.24


def _line_z(x):
    """How high the line hangs at x: from the post tops, sagging in the middle."""
    return _LINE_TOP - 0.12 * (1.0 - (x / _LINE_X) ** 2)


def _line():
    n = 18
    for k in range(n):
        x0 = -_LINE_X + 2 * _LINE_X * k / n
        x1 = -_LINE_X + 2 * _LINE_X * (k + 1) / n
        z0, z1 = _line_z(x0), _line_z(x1)
        length = math.hypot(x1 - x0, z1 - z0)
        blk("line_%d" % k, (length + 0.01, 0.03, 0.03), ((x0 + x1) / 2, 0, (z0 + z1) / 2), "rope",
            rot=(0, -math.atan2(z1 - z0, x1 - x0), 0), bev=0)


def prop_laundry_posts():
    """A washing line's two posts, and the line between them."""
    for sx in (-1, 1):
        blk("post_%d" % sx, (0.09, 0.09, 1.30), (sx * _LINE_X, 0, 0.65), "oak_light")
        blk("cap_%d" % sx, (0.13, 0.13, 0.05), (sx * _LINE_X, 0, 1.32), "oak")
        blk("foot_%d" % sx, (0.16, 0.16, 0.06), (sx * _LINE_X, 0, 0.03), "stone")
    _line()
    return 2.6


def _peg(name, x):
    blk(name, (0.035, 0.05, 0.09), (x, -0.03, _line_z(x) - 0.02), "wood_cut", bev=0)


def prop_laundry_wash():
    """What is pegged out on the line: a sheet, a shirt, a pair of breeches, a
    striped towel and two stockings. The line is drawn again here, still, so
    the cloth hangs from something whichever picture is on top."""
    _line()
    # The sheet, folded over the line: two layers, the back one a shade darker.
    x0, x1 = -0.98, -0.40
    top = _line_z((x0 + x1) / 2)
    blk("sheet_back", (x1 - x0, 0.02, 0.60), ((x0 + x1) / 2, 0.02, top - 0.30), "linen_dk", bev=0)
    blk("sheet", (x1 - x0 - 0.04, 0.02, 0.52), ((x0 + x1) / 2 - 0.02, -0.01, top - 0.26), "linen", bev=0)
    for x in (x0 + 0.06, x1 - 0.06):
        _peg("sheet_peg_%d" % int(x * 100), x)
    # The shirt, hung by its tails: body, two sleeves hanging down and out.
    cx = -0.10
    top = _line_z(cx)
    blk("shirt", (0.34, 0.02, 0.40), (cx, -0.01, top - 0.21), "cloth_sky", bev=0)
    for sx in (-1, 1):
        blk("sleeve_%d" % sx, (0.10, 0.02, 0.30), (cx + sx * 0.21, -0.01, top - 0.30), "cloth_sky",
            rot=(0, sx * math.radians(18), 0), bev=0)
    blk("collar", (0.14, 0.02, 0.05), (cx, -0.02, top - 0.40), "linen", bev=0)
    for x in (cx - 0.14, cx + 0.14):
        _peg("shirt_peg_%d" % int(x * 100 + 200), x)
    # Breeches, by the waistband.
    cx = 0.34
    top = _line_z(cx)
    blk("waist", (0.28, 0.02, 0.06), (cx, -0.01, top - 0.04), "cloth_ochre", bev=0)
    for sx in (-1, 1):
        blk("leg_%d" % sx, (0.12, 0.02, 0.44), (cx + sx * 0.075, -0.01, top - 0.28), "cloth_ochre", bev=0)
    _peg("breeches_peg_a", cx - 0.12)
    _peg("breeches_peg_b", cx + 0.12)
    # A towel with a stripe across it.
    cx = 0.70
    top = _line_z(cx)
    blk("towel", (0.24, 0.02, 0.34), (cx, -0.01, top - 0.18), "linen", bev=0)
    blk("towel_stripe", (0.24, 0.02, 0.05), (cx, -0.02, top - 0.26), "cloth_rose", bev=0)
    _peg("towel_peg", cx)
    # And a pair of stockings.
    for k, cx in enumerate((0.93, 1.03)):
        top = _line_z(cx)
        blk("stocking_%d" % k, (0.06, 0.02, 0.22), (cx, -0.01, top - 0.12), "cloth_sage", bev=0)
        blk("stocking_foot_%d" % k, (0.09, 0.02, 0.05), (cx + 0.02, -0.01, top - 0.22), "cloth_sage", bev=0)
        _peg("stocking_peg_%d" % k, cx)
    return 2.6


PROPS = {
    "laundry_posts":   (prop_laundry_posts, 80),
    "laundry_wash":    (prop_laundry_wash, 80),
    "fisher_cottage_a": (prop_fisher_cottage_a, 128),
    "fisher_cottage_b": (prop_fisher_cottage_b, 128),
    "fisher_cottage_wrecked":  (prop_fisher_cottage_wrecked, 128),
    "fisher_cottage_breached": (prop_fisher_cottage_breached, 128),
    "ferry_house":     (prop_ferry_house, 160),
    "boat_shed":       (prop_boat_shed, 128),
    "net_rack":        (prop_net_rack, 64),
    "post_lantern":    (prop_post_lantern, 64),
    "wattle_fence":    (prop_wattle_fence, 64),
    "brick_wall":      (prop_brick_wall, 64),
    "brick_wall_side": (prop_brick_wall_side, 64),
    "brick_pier":      (prop_brick_pier, 80),
    "dwarf_mine_gate": (prop_dwarf_mine_gate, 192),
    "mine_support":    (prop_mine_support, 80),
    "mine_lantern":    (prop_mine_lantern, 56),
    "dwarf_statue":    (prop_dwarf_statue, 80),
    "inn_window":    (prop_inn_window, 64),
    "barrel_table":  (prop_barrel_table, 48),
    "armchair":      (prop_armchair, 64),
    "stage":         (prop_stage, 96),
    "town_map":      (prop_town_map, 56),
    "town_well":     (prop_town_well, 136),
    "street_lamp":   (prop_street_lamp, 96),
    "planter":       (prop_planter, 48),
    "park_bench":    (prop_park_bench, 56),
    "mayor_hall":    (prop_mayor_hall, 256),
    "townhouse_a":   (prop_townhouse_a, 144),
    "townhouse_b":   (prop_townhouse_b, 144),
    "townhouse_c":   (prop_townhouse_c, 176),
    "townhouse_d":   (prop_townhouse_d, 144),
    "birch_tree":    (prop_birch_tree, 112),
    "stump_birch":   (prop_stump_birch, 40),
    "stump_charred": (prop_stump_charred, 40),
}
