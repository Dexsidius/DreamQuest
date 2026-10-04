# =============================================================================
#  blender_prologue_props.py - what the prologue's scenes stand on and in: the
#  handcart the player is found and carried on, the town gone quiet (a stall
#  shut, sleepers in their beds, a dark window, the mayor's papers on the
#  floor and his tea gone cold), the stranger's dungeon (the cell door open and
#  rotted, the mattress, the chains, what is left of earlier prisoners, the
#  light down through the grate), and his house (the foyer's stained glass,
#  the armour's pedestals, the great doors, the manor itself from outside, its
#  gate and fence, and the dead trees and the overturned cart on the hill).
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only handcart,stall_closed
#
#  Built with blender_props.py's own tools and registered into its PROPS
#  table; that file hands itself over (see where PROPS is assembled there).
#  The pools of light on the floor are not here: they are mostly transparent,
#  which a render cut to on-or-off alpha cannot be, and are drawn by
#  tools/make_light_pools.ps1.
#
#  Sizes that are not square are cut to size by make_props.ps1 ($CROP).
# =============================================================================

import math
import random

import bpy  # noqa: F401  (the parts are Blender objects)
from mathutils import Euler, Matrix, Vector

import blender_props as bp

blk, cyl, cone, sphere, material = bp.blk, bp.cyl, bp.cone, bp.sphere, bp.material
BUILDING = bp.BUILDING_ELEVATION

bp.PALETTE.update({
    # A villager's handcart: weathered boards, darker rails, iron tyres.
    "cart_board":    (0.560, 0.420, 0.280), "cart_board_dk": (0.420, 0.300, 0.190),
    "cart_board_lt": (0.660, 0.520, 0.360), "cart_tyre":     (0.250, 0.250, 0.270),
    # Sleepers: night shirts, hair, skin, and the shut eye.
    "shirt_white":   (0.880, 0.860, 0.800), "shirt_blue":    (0.620, 0.680, 0.760),
    "skin":          (0.930, 0.760, 0.620), "skin_old":      (0.860, 0.720, 0.620),
    "hair_dark":     (0.200, 0.160, 0.130), "hair_auburn":   (0.620, 0.280, 0.140),
    "hair_white":    (0.920, 0.910, 0.880), "eye_shut":      (0.300, 0.200, 0.180),
    # A room with its curtains drawn.
    "curtain":       (0.420, 0.160, 0.160), "curtain_dk":    (0.300, 0.110, 0.120),
    "curtain_lt":    (0.520, 0.230, 0.220), "pane_dim":      (0.240, 0.270, 0.320),
    "ink":           (0.180, 0.170, 0.220), "china":         (0.920, 0.920, 0.900),
    "china_blue":    (0.360, 0.480, 0.700), "tea":           (0.520, 0.320, 0.160),
})


def ring(name, major, minor, loc, colour, rot=(0, 0, 0), rough=0.8, metal=0.0, emit=0.0):
    """A torus: a tyre, a hoop, a manacle, a ring in a wall. Lies in XY unless turned."""
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, location=loc, rotation=rot,
                                     major_segments=32, minor_segments=10)
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(material(name, colour, rough, metal, emit))
    return ob


# =================================================================================
#  Scene 1 -- the road: the handcart
# =================================================================================
def _wheel(name, x, y, z, r, rim="cart_board_dk", spokes=3, rot=None):
    """A big cart wheel standing in the YZ plane (or turned by `rot`): an iron
    tyre, a wooden felloe, a hub and its spokes, open between them. Chunky,
    because a spoke at true thickness is no pixels at all."""
    turn = rot or (0, math.pi / 2, 0)
    ring(name + "_tyre", r - 0.03, 0.035, (x, y, z), "cart_tyre", rot=turn)
    ring(name + "_felloe", r - 0.08, 0.035, (x, y, z), rim, rot=turn)
    hub = cyl(name + "_hub", 0.085, 0.18, (x, y, z), "cart_board", rot=turn, verts=12)
    for k in range(spokes):
        a = k / spokes * math.pi
        sp = blk("%s_spoke_%d" % (name, k), (0.075, 0.075, 2 * (r - 0.09)), (x, y, z), "cart_board", bev=0)
        # Spokes across the wheel's own face, whichever way the wheel is turned.
        spin = Matrix.Rotation(a, 4, "X")
        face = Euler(turn, "XYZ").to_matrix().to_4x4() @ Euler((0, -math.pi / 2, 0), "XYZ").to_matrix().to_4x4()
        sp.rotation_euler = (face @ spin).to_euler("XYZ")
    return hub


def _cart_bed(prefix="", broken=False):
    """The bed and its low slatted sides, the axle under the middle, the two
    shafts out in front and the stand that holds it level when nobody does."""
    BW, BL, Z = 0.86, 1.24, 0.56
    blk(prefix + "floor", (BW, BL, 0.06), (0, 0.06, Z), "cart_board_lt")
    for k in range(5):
        blk(prefix + "plank_%d" % k, (0.012, BL - 0.04, 0.012), (-BW / 2 + 0.17 + k * 0.13, 0.06, Z + 0.035),
            "cart_board_dk", bev=0)
    # Low slatted sides: a top rail on short stakes, on three sides; the front
    # is open where the shafts are.
    for sx in (-1, 1):
        blk(prefix + "rail_%d" % sx, (0.06, BL, 0.06), (sx * (BW / 2 - 0.03), 0.06, Z + 0.26), "cart_board")
        blk(prefix + "slat_%d" % sx, (0.04, BL, 0.08), (sx * (BW / 2 - 0.03), 0.06, Z + 0.10), "cart_board_dk")
        for k in range(4):
            blk(prefix + "stake_%d_%d" % (sx, k), (0.06, 0.06, 0.28), (sx * (BW / 2 - 0.03), -0.50 + k * 0.38, Z + 0.15),
                "cart_board_dk")
    if not broken:
        blk(prefix + "tail", (BW, 0.05, 0.08), (0, 0.06 + BL / 2 - 0.03, Z + 0.26), "cart_board")
    blk(prefix + "tail_slat", (BW, 0.04, 0.08), (0, 0.06 + BL / 2 - 0.03, Z + 0.10), "cart_board_dk")
    for sx in (-1, 1):
        blk(prefix + "tail_stake_%d" % sx, (0.06, 0.06, 0.28), (sx * 0.18, 0.06 + BL / 2 - 0.03, Z + 0.15),
            "cart_board_dk")
    blk(prefix + "axle", (BW + 0.36, 0.08, 0.08), (0, 0.10, 0.42), "cart_board_dk")
    # The shafts: long, out towards whoever pulls it, a grip bar across the ends.
    for sx in (-1, 1):
        blk(prefix + "shaft_%d" % sx, (0.07, 1.30, 0.07), (sx * 0.34, -0.95, Z - 0.03), "cart_board")
        blk(prefix + "stand_%d" % sx, (0.06, 0.06, 0.48), (sx * 0.34, -0.48, 0.25), "cart_board_dk")
    blk(prefix + "grip", (0.80, 0.07, 0.07), (0, -1.56, Z - 0.03), "cart_board_lt")
    return BW, Z


def prop_handcart():
    """A villager's two-wheeled handcart: a flat bed with low slatted sides on
    two big spoked wheels, and two long shafts out towards the camera to pull
    it by. Big enough to carry a body, which is what it does in the opening."""
    BW, Z = _cart_bed()
    # Seen from in front a wheel is edge on -- a dark bar either side of a
    # box. So each is turned a third of the way round to the camera: wrong on
    # a real cart, and the only way it reads as wheels at sixty pixels.
    for sx in (-1, 1):
        _wheel("wheel_%d" % sx, sx * (BW / 2 + 0.14), 0.10, 0.42, 0.42, rot=(0, math.pi / 2, -sx * math.radians(38)))
    return 2.30


# =================================================================================
#  Scene 2/3 -- the town: the stall shut up
# =================================================================================
def prop_stall_closed():
    """The market stall from the square (market_stall), shut: the awning rolled
    up tight against its bar, boards over the front of the counter, a shutter
    leant against it, nothing on it to sell. The red and cream of the awning is
    still what says it is the same stall."""
    W, D = 1.70, 0.80
    blk("counter", (W, D, 0.10), (0, 0, 0.84), "oak_light")
    blk("counter_front", (W, 0.06, 0.78), (0, -D / 2 + 0.03, 0.42), "oak")
    for x in (-W / 2 + 0.05, W / 2 - 0.05):
        for y in (-D / 2 + 0.05, D / 2 - 0.05):
            blk("post_%.1f_%.1f" % (x, y), (0.08, 0.08, 1.80), (x, y, 0.90), "log")
    # Boards nailed across the counter, a little crooked, and the bar they hang
    # on: the stall's front is closed up.
    for k, (z, tilt) in enumerate(((1.02, 2), (1.22, -3), (1.42, 1.5))):
        blk("board_%d" % k, (W + 0.06, 0.05, 0.17), (0, -D / 2 - 0.02, z), "oak_pale" if k % 2 else "oak_light",
            rot=(0, math.radians(tilt), 0), bev=0.01)
        for sx in (-1, 1):
            blk("nail_%d_%d" % (k, sx), (0.03, 0.02, 0.03), (sx * (W / 2 - 0.10), -D / 2 - 0.05, z), "iron", bev=0)
    # The awning furled: rolled tight to the top bar in its stripes, a strap
    # round each end of the roll.
    blk("awning_bar", (W + 0.20, 0.08, 0.08), (0, -D / 2 + 0.02, 1.80), "log")
    stripes = 9
    for i in range(stripes):
        x = -W / 2 - 0.08 + (i + 0.5) * (W + 0.16) / stripes
        cyl("roll_%d" % i, 0.13, (W + 0.16) / stripes + 0.004, (x, -D / 2 - 0.04, 1.90),
            ("cloth_red", "cloth_cream")[i % 2], rot=(0, math.pi / 2, 0), verts=14)
    for sx in (-1, 1):
        cyl("strap_%d" % sx, 0.14, 0.06, (sx * 0.55, -D / 2 - 0.04, 1.90), "leather", rot=(0, math.pi / 2, 0), verts=14)
    # A shutter leant against the side, and an empty crate turned over.
    blk("shutter", (0.50, 0.05, 0.90), (W / 2 + 0.22, -0.20, 0.44), "oak", rot=(math.radians(-12), 0, math.radians(-20)))
    for k in range(3):
        blk("shutter_slat_%d" % k, (0.46, 0.03, 0.04), (W / 2 + 0.215, -0.25, 0.20 + k * 0.25), "oak_light",
            rot=(math.radians(-12), 0, math.radians(-20)), bev=0)
    blk("crate", (0.40, 0.36, 0.34), (-W / 2 - 0.28, -0.12, 0.17), "oak", rot=(0, 0, math.radians(14)))
    return 2.9


# =================================================================================
#  Scene 4/5/6 -- homes and the Mayor's Hall
# =================================================================================
def _sleeper(kind):
    """bed_single's bed, exactly -- the same builder and the same framing, so
    a sleeper's bed and an empty one are the same pixels where they show -- and
    someone asleep in it: the blanket over the body to the chest, the head on
    the pillow, eyes shut, one arm out over the blanket."""
    bp.bed(0.92, "cloth_red")
    # The body under the blanket: a long mound, the knees and feet showing
    # through it, all in the blanket's red.
    mound = sphere("mound", 0.5, (0, -0.22, 0.60), "cloth_red")
    mound.scale = (0.40, 0.92, 0.18)
    for sx in (-1, 1):
        sphere("foot_%d" % sx, 0.13, (sx * 0.11, -0.80, 0.60), "cloth_red").scale = (1.0, 1.0, 0.6)
    sphere("chest", 0.22, (0, 0.18, 0.60), "cloth_red").scale = (1.15, 0.9, 0.55)
    # The fold of the sheet at the chest, as on the empty bed.
    blk("sheet_fold", (0.84, 0.16, 0.10), (0, 0.30, 0.66), "cloth_cream", bev=0.04)
    # The head on the pillow, face up: forehead to the headboard, chin to the
    # feet, so the camera looks down the face.
    shirt = {"man": "shirt_blue", "woman": "shirt_white", "elder": "shirt_white"}[kind]
    skin = "skin_old" if kind == "elder" else "skin"
    hz = 0.80
    sphere("neck", 0.07, (0, 0.40, 0.70), skin)
    head = sphere("head", 0.15, (0, 0.55, hz), skin)
    head.scale = (1.0, 1.05, 0.85)
    sphere("nose", 0.04, (0, 0.50, hz + 0.12), skin)
    for sx in (-1, 1):
        blk("eye_%d" % sx, (0.095, 0.07, 0.035), (sx * 0.068, 0.55, hz + 0.115), "eye_shut", bev=0.006)
        sphere("ear_%d" % sx, 0.04, (sx * 0.15, 0.57, hz), skin)
    if kind == "man":
        # Dark hair, short: a cap over the crown on the pillow side.
        hair = sphere("hair", 0.165, (0, 0.62, hz - 0.01), "hair_dark")
        hair.scale = (1.05, 0.90, 0.80)
        blk("fringe", (0.24, 0.07, 0.05), (0, 0.66, hz + 0.10), "hair_dark", bev=0.02)
    elif kind == "woman":
        # Long auburn hair spread out over the pillow round her head.
        hair = sphere("hair", 0.17, (0, 0.62, hz - 0.01), "hair_auburn")
        hair.scale = (1.08, 0.95, 0.82)
        for k, (x, y, a) in enumerate(((-0.24, 0.66, 30), (0.24, 0.66, -30), (-0.30, 0.52, 70), (0.30, 0.52, -70),
                                       (-0.14, 0.78, 10), (0.14, 0.78, -10))):
            lock = blk("lock_%d" % k, (0.13, 0.20, 0.05), (x, y, 0.72), "hair_auburn", rot=(0, 0, math.radians(a)),
                       bev=0.04)
        blk("fringe", (0.26, 0.07, 0.05), (0, 0.67, hz + 0.10), "hair_auburn", bev=0.02)
    else:
        # Bald, a fringe of white round the back, and a white beard out over
        # the sheet.
        for sx in (-1, 1):
            sphere("tuft_%d" % sx, 0.07, (sx * 0.13, 0.63, hz - 0.02), "hair_white")
        sphere("beard", 0.12, (0, 0.38, hz - 0.02), "hair_white").scale = (1.1, 1.3, 0.6)
        sphere("moustache", 0.06, (0, 0.46, hz + 0.08), "hair_white").scale = (1.4, 0.7, 0.6)
        for sx in (-1, 1):
            blk("brow_%d" % sx, (0.07, 0.04, 0.025), (sx * 0.065, 0.585, hz + 0.135), "hair_white", bev=0.008)
    # One arm out over the blanket: a sleeve and a hand lying across the body.
    blk("arm", (0.11, 0.42, 0.09), (0.20, 0.02, 0.68), shirt, rot=(0, 0, math.radians(-24)), bev=0.04)
    sphere("hand", 0.065, (0.12, -0.18, 0.69), skin)
    blk("cuff", (0.12, 0.05, 0.10), (0.15, -0.14, 0.69), shirt, rot=(0, 0, math.radians(-24)), bev=0.02)
    return 2.35


def prop_bed_sleeper_man():
    return _sleeper("man")


def prop_bed_sleeper_woman():
    return _sleeper("woman")


def prop_bed_sleeper_elder():
    return _sleeper("elder")


def prop_window_curtained():
    """A window in the back wall with heavy curtains drawn right across it, a
    little grey light at the edges and nothing else: the room it is in is
    dark in the middle of the day."""
    blk("frame", (0.92, 0.10, 1.06), (0, 0.04, 1.00), "oak")
    blk("glass", (0.78, 0.04, 0.90), (0, -0.01, 1.00), "pane_dim", rough=0.3, bev=0)
    blk("sill", (1.06, 0.22, 0.07), (0, -0.07, 0.44), "oak_light")
    blk("under", (0.98, 0.10, 0.38), (0, 0.04, 0.22), "oak", bev=0)
    # The rod over it, with finials, and the two curtains meeting in the
    # middle: heavy cloth in folds -- alternate light and dark -- to the sill.
    cyl("rod", 0.025, 1.24, (0, -0.10, 1.58), "iron", rot=(0, math.pi / 2, 0), verts=10)
    for sx in (-1, 1):
        sphere("finial_%d" % sx, 0.045, (sx * 0.63, -0.10, 1.58), "brass", rough=0.4)
        for k in range(5):
            x = sx * (0.04 + k * 0.115)
            fold = ("curtain", "curtain_dk", "curtain_lt", "curtain_dk", "curtain")[k]
            blk("curtain_%d_%d" % (sx, k), (0.125, 0.09 if k % 2 else 0.12, 1.14), (x, -0.12 - 0.015 * (k % 2), 1.00),
                fold, bev=0.03)
        # Gathered a little at the foot, where the cloth lies on the sill.
        blk("hem_%d" % sx, (0.56, 0.13, 0.06), (sx * 0.30, -0.12, 0.45), "curtain_dk", bev=0.02)
    blk("pelmet", (1.10, 0.10, 0.14), (0, -0.14, 1.62), "curtain_dk", bev=0.02)
    return 1.95


def prop_papers_scattered():
    """The mayor's papers, fallen off his desk and over the floor: loose sheets
    every which way, some with writing on them, one crumpled."""
    rng = random.Random(17)
    sheets = ((-0.42, 0.10, 18), (-0.10, -0.12, -32), (0.28, 0.06, 64), (0.50, -0.14, -12), (0.02, 0.22, 8),
              (-0.36, -0.20, 52), (0.18, -0.28, -48))
    for k, (x, y, a) in enumerate(sheets):
        z = 0.01 + k * 0.004
        blk("sheet_%d" % k, (0.30, 0.38, 0.012), (x, y, z), "paper", rot=(0, 0, math.radians(a)), bev=0.004)
        if k % 3 != 2:
            for j in range(3):
                dx = math.cos(math.radians(a)) * 0.0 - math.sin(math.radians(a)) * (0.10 - j * 0.07)
                dy = math.sin(math.radians(a)) * 0.0 + math.cos(math.radians(a)) * (0.10 - j * 0.07)
                blk("line_%d_%d" % (k, j), (0.20 - rng.uniform(0, 0.06), 0.022, 0.006), (x + dx, y + dy, z + 0.008),
                    "ink", rot=(0, 0, math.radians(a)), bev=0)
    crumple = sphere("crumple", 0.08, (0.56, 0.20, 0.06), "paper")
    crumple.scale = (1.0, 0.9, 0.7)
    return 1.55


def prop_teacup():
    """A cup on its saucer, the tea in it gone cold: white china with a blue
    band, and the handle out to the right so it reads as a cup."""
    cyl("saucer", 0.20, 0.03, (0, 0, 0.02), "china", verts=24)
    cyl("saucer_well", 0.10, 0.032, (0, 0, 0.022), "china_blue", verts=20)
    cyl("cup", 0.12, 0.16, (0, 0, 0.12), "china", verts=24)
    cyl("cup_band", 0.123, 0.035, (0, 0, 0.16), "china_blue", verts=24)
    cyl("tea", 0.105, 0.02, (0, 0, 0.195), "tea", verts=20)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.06, minor_radius=0.022, location=(0.15, 0, 0.13),
                                     rotation=(math.pi / 2, 0, 0))
    handle = bpy.context.active_object
    handle.data.materials.append(material("handle", "china"))
    return 0.50


# =================================================================================
#  Scene 7/8/9 -- the cell and the dungeon
# =================================================================================
bp.PALETTE.update({
    "rust_stain":    (0.560, 0.290, 0.140), "rust_stain_dk": (0.380, 0.190, 0.100),
    "ticking":       (0.640, 0.590, 0.450), "ticking_dk":    (0.500, 0.450, 0.330),
    "stain":         (0.380, 0.330, 0.230), "straw_dk":      (0.640, 0.540, 0.300),
    "rag":           (0.450, 0.400, 0.330), "rag_dk":        (0.320, 0.280, 0.230),
    "pris_skin":     (0.700, 0.660, 0.620), "pris_skin_dk":  (0.540, 0.510, 0.480),
    "pris_hair":     (0.380, 0.340, 0.300),
    "bone_dk":       (0.660, 0.630, 0.540),
})


def _bars_leaf(prefix=""):
    """cell_door's door: the bars and their rails, the heavier frame, the
    brace and the hinges (the lock is the caller's). Returns its objects."""
    before = set(bpy.context.scene.objects)
    blk(prefix + "rail_lo", (2.0, 0.10, 0.08), (0, 0, 0.3), "pal_iron", bev=0)
    blk(prefix + "rail_hi", (2.0, 0.10, 0.08), (0, 0, 2.08), "pal_iron", bev=0)
    for k in range(7):
        cyl(prefix + "bar_%d" % k, 0.045, 2.1, (-0.86 + k * 0.287, 0, 1.2), "pal_iron", verts=8)
        cone(prefix + "tip_%d" % k, 0.05, 0.1, (-0.86 + k * 0.287, 0, 0.2), "pal_iron", verts=6, rot=(math.pi, 0, 0))
    blk(prefix + "frame_l", (0.12, 0.14, 2.0), (-0.9, -0.03, 1.2), "pal_iron_lt", bev=0)
    blk(prefix + "frame_r", (0.12, 0.14, 2.0), (0.9, -0.03, 1.2), "pal_iron_lt", bev=0)
    blk(prefix + "brace", (1.8, 0.12, 0.10), (0, -0.03, 1.2), "pal_iron_lt", bev=0)
    for z in (0.6, 1.8):
        blk(prefix + "hinge_%d" % int(z * 10), (0.20, 0.16, 0.12), (-0.96, -0.04, z), "pal_iron", bev=0)
    return [ob for ob in bpy.context.scene.objects if ob not in before]


def _cell_stone():
    """The stone the door hangs in: the sill and the head, as cell_bars has them."""
    blk("sill", (2.0, 0.3, 0.14), (0, 0, 0.07), "pal_stone_dk", bev=0.01)
    blk("head", (2.0, 0.3, 0.2), (0, 0, 2.3), "pal_stone_dk", bev=0.01)


def prop_cell_door_open():
    """cell_door, swung open on its hinges: the stone sill and head where they
    were, the opening empty between them, and the door turned back about its
    hinge side into the far side of the wall. Back, and not out towards the
    camera: swung out, its foot would be the lowest thing in the picture, and
    the frame would jump when the game swaps this for the shut door. Built low
    so the leaf's top, which rises up the picture as it turns away, stays in
    the frame; make_props.ps1 sits it back on the floor."""
    drop = -0.50
    _cell_stone()
    for sx in (-1, 1):
        blk("jamb_%d" % sx, (0.10, 0.30, 2.16), (sx * 1.03, 0, 1.15), "pal_stone_dk", bev=0.01)
    leaf = _bars_leaf()
    blk("lock", (0.26, 0.18, 0.30), (0.7, -0.08, 1.2), "pal_iron", bev=0.01)
    sphere("keyhole", 0.04, (0.7, -0.18, 1.2), "pal_void")
    leaf += [ob for ob in bpy.context.scene.objects if ob.name.startswith(("lock", "keyhole"))]
    pivot = bpy.data.objects.new("hinge_pivot", None)
    bpy.context.collection.objects.link(pivot)
    pivot.location = (-0.96, 0.0, 0.0)
    for ob in leaf:
        ob.parent = pivot
        ob.location = ob.location - pivot.location
    pivot.rotation_euler = (0, 0, math.radians(52))
    for ob in list(bpy.context.scene.objects):
        if ob.type == "MESH" and ob.parent is None:
            ob.location.z += drop
    pivot.location.z += drop
    # Framed wider at the same scale as cell_door (2.6 units to 84 pixels), so
    # the bars are the same bars: make_props.ps1 cuts it to 84 wide and 100
    # tall, the extra rows above for the top of the leaf as it turns away.
    return 2.6 * 100 / 84


def prop_cell_door_rotted():
    """cell_door in the Reverie: shut, but where the lock was there is only a
    stain -- rust run down the bars and the brace from a box that has rusted
    clean away, and the stub of the bolt."""
    _cell_stone()
    _bars_leaf()
    # The stain where the lock box was, and the runs of it down the bars.
    # Flat on the brace and the bars, blotched, and run down them: a stain the
    # shape of nothing much, which is the point.
    for k, (x, z, r) in enumerate(((0.62, 1.24, 0.07), (0.74, 1.16, 0.08), (0.80, 1.27, 0.05), (0.66, 1.12, 0.05))):
        st = sphere("blot_%d" % k, r, (x, -0.085, z), "rust_stain_dk" if k % 2 else "rust_stain")
        st.scale = (1.3, 0.25, 1.0)
    for k, (x, ln) in enumerate(((0.57, 0.40), (0.86, 0.30), (0.72, 0.22))):
        blk("run_%d" % k, (0.045, 0.10, ln), (x, -0.02, 1.08 - ln / 2), "rust_stain", bev=0)
    # Where the bolt went through the frame: two empty holes.
    for k, z in enumerate((1.12, 1.28)):
        sphere("bolt_hole_%d" % k, 0.025, (0.90, -0.11, z), "pal_void")
    return 2.6


def prop_mattress():
    """A thin straw pad laid on the stone: ticking gone the colour of the
    floor, stained, flattened, straw coming out of a split seam."""
    rng = random.Random(23)
    W, D = 1.22, 0.86
    blk("pad", (W, D, 0.08), (0, 0, 0.04), "ticking", bev=0.04)
    blk("pad_top", (W - 0.10, D - 0.10, 0.03), (0, 0, 0.075), "ticking", bev=0.02)
    for k in range(3):
        blk("tuft_seam_%d" % k, (0.012, D - 0.12, 0.012), (-W / 2 + 0.31 + k * 0.30, 0, 0.09), "ticking_dk", bev=0)
    for k, (x, y, r) in enumerate(((-0.30, 0.10, 0.17), (0.22, -0.16, 0.13), (0.36, 0.20, 0.09))):
        st = sphere("stain_%d" % k, r, (x, y, 0.07), "stain")
        st.scale = (1.2, 0.8, 0.12)
    for k in range(9):
        x = W / 2 - 0.02
        y = rng.uniform(-0.30, 0.30)
        blk("straw_%d" % k, (0.18, 0.02, 0.02), (x + 0.05, y, 0.05), "straw" if k % 2 else "straw_dk",
            rot=(0, 0, rng.uniform(-0.6, 0.6)), bev=0)
    return 1.33


def prop_wall_chains():
    """Two chains from one iron ring in the wall, each ending in a manacle,
    hanging open: for whoever is put here next."""
    blk("plate", (0.20, 0.05, 0.20), (0, 0.11, 1.66), "pal_iron", bev=0.01)
    ring("ring", 0.11, 0.035, (0, 0.03, 1.56), "pal_iron_lt", rot=(math.radians(70), 0, 0))
    for sx in (-1, 1):
        for k in range(6):
            z = 1.46 - k * 0.12
            x = sx * (0.05 + k * 0.035)
            blk("link_%d_%d" % (sx, k), (0.07 if k % 2 else 0.035, 0.035 if k % 2 else 0.07, 0.14), (x, 0.0, z),
                "pal_iron_lt" if k % 2 else "pal_iron", bev=0.01)
        ring("cuff_%d" % sx, 0.085, 0.032, (sx * 0.27, -0.02, 0.66), "pal_iron", rot=(math.radians(70), 0, sx * 0.4))
        blk("cuff_hinge_%d" % sx, (0.05, 0.05, 0.05), (sx * 0.27, -0.04, 0.75), "pal_iron_lt", bev=0)
    blk("streak", (0.10, 0.03, 0.50), (0, 0.13, 1.30), "rust_stain_dk", bev=0)
    # Built low: make_props.ps1 sits it on the bottom of its picture anyway,
    # and at its true height the ring ran off the top of the frame.
    for ob in list(bpy.context.scene.objects):
        if ob.type == "MESH":
            ob.location.z -= 0.62
    return 1.60


def _skull(name, x, y, z, tilt=(0, 0, 0), r=0.12):
    head = sphere(name, r, (x, y, z), "pal_bone")
    head.scale = (0.85, 1.0, 0.9)
    head.rotation_euler = tilt
    face = Euler(tilt, "XYZ").to_matrix()
    for sx in (-1, 1):
        off = face @ Vector((sx * 0.045, -r * 0.88, 0.02))
        sphere("%s_socket_%d" % (name, sx), r * 0.28, (x + off.x, y + off.y, z + off.z), "pal_void")
    off = face @ Vector((0, -r * 0.80, -0.07))
    blk(name + "_jaw", (r * 1.1, r * 0.6, r * 0.35), (x + off.x, y + off.y, z + off.z), "bone_dk", rot=tilt, bev=0.02)
    return head


def _ribs(name, x, y, z, rot=(0, 0, 0), n=4, w=0.17):
    """A ribcage: a spine down the middle and hoops of rib either side."""
    m = Euler(rot, "XYZ").to_matrix()
    spine_a, spine_b = m @ Vector((0, 0, 0.18)), m @ Vector((0, 0, -0.20))
    blk(name + "_spine", (0.05, 0.05, 0.42), (x, y, z), "bone_dk", rot=rot, bev=0.01)
    for k in range(n):
        off = m @ Vector((0, -0.02, 0.13 - k * 0.09))
        ring("%s_rib_%d" % (name, k), w - abs(k - 1) * 0.02, 0.022, (x + off.x, y + off.y, z + off.z), "pal_bone",
             rot=rot)
    return spine_a, spine_b


def prop_skeleton_remains():
    """Somebody who did not get out: a skeleton on its back on the stone, in
    what is left of its clothes, the skull rolled to one side and an arm
    flung out, a few bones gone astray."""
    blk("rag", (0.70, 0.46, 0.03), (0.02, 0.0, 0.015), "rag_dk", rot=(0, 0, 0.12), bev=0.02)
    blk("rag_b", (0.36, 0.30, 0.03), (-0.34, 0.08, 0.02), "rag", rot=(0, 0, -0.4), bev=0.02)
    _skull("skull", -0.62, 0.02, 0.10, tilt=(math.radians(-70), 0, math.radians(-30)))
    _ribs("cage", -0.20, 0.0, 0.10, rot=(0, math.radians(90), 0), n=4, w=0.15)
    sphere("pelvis", 0.12, (0.20, 0.0, 0.06), "bone_dk").scale = (0.9, 1.3, 0.45)
    # Legs out to the right, one bent; an arm flung up and one along the body.
    for k, (x, y, a, ln) in enumerate(((0.50, -0.07, 0.10, 0.42), (0.45, 0.12, -0.35, 0.40),
                                       (0.80, -0.04, -0.05, 0.36), (0.72, 0.28, 0.6, 0.34))):
        cyl("leg_%d" % k, 0.028, ln, (x, y, 0.05), "pal_bone", rot=(0, math.pi / 2, a), verts=8)
    cyl("arm_up", 0.024, 0.34, (-0.30, -0.26, 0.05), "pal_bone", rot=(0, math.pi / 2, 0.9), verts=8)
    cyl("arm_down", 0.024, 0.34, (0.00, 0.22, 0.05), "pal_bone", rot=(0, math.pi / 2, -0.15), verts=8)
    for k, (x, y) in enumerate(((-0.12, -0.36), (0.30, -0.30), (-0.44, 0.30))):
        cyl("stray_%d" % k, 0.02, 0.14, (x, y, 0.03), "bone_dk", rot=(0, math.pi / 2, k * 1.1), verts=6)
    return 1.62


def prop_skeleton_chained():
    """A skeleton sat on the floor against the wall with its wrists still in
    the irons: the arms held up and apart by chains to two rings, the ribs
    sagging, the skull fallen forward onto them."""
    for sx in (-1, 1):
        ring("wall_ring_%d" % sx, 0.07, 0.025, (sx * 0.36, 0.12, 1.36), "pal_iron_lt", rot=(math.pi / 2, 0, 0))
        for k in range(3):
            blk("link_%d_%d" % (sx, k), (0.05, 0.05, 0.10), (sx * (0.35 - k * 0.012), 0.08, 1.26 - k * 0.09),
                "pal_iron", bev=0.01)
        ring("cuff_%d" % sx, 0.06, 0.025, (sx * 0.33, 0.05, 0.98), "pal_iron", rot=(0, 0, 0))
        # Arms up to the irons from the shoulders.
        cyl("upper_%d" % sx, 0.03, 0.30, (sx * 0.24, 0.02, 0.80), "pal_bone", rot=(0, -sx * 0.55, 0), verts=8)
        cyl("fore_%d" % sx, 0.026, 0.24, (sx * 0.31, 0.04, 1.02), "pal_bone", rot=(0, -sx * 0.25, 0), verts=8)
        # Legs splayed out across the floor to either side: pointed at the
        # camera they foreshortened into the legs of a stool.
        cyl("thigh_%d" % sx, 0.038, 0.36, (sx * 0.24, -0.14, 0.05), "pal_bone",
            rot=(0, math.pi / 2, -sx * math.radians(32)), verts=8)
        cyl("shin_%d" % sx, 0.032, 0.34, (sx * 0.50, -0.30, 0.04), "pal_bone",
            rot=(0, math.pi / 2, -sx * math.radians(52)), verts=8)
        sphere("kneecap_%d" % sx, 0.045, (sx * 0.38, -0.22, 0.06), "bone_dk")
    blk("rag_lap", (0.42, 0.34, 0.06), (0, -0.08, 0.06), "rag_dk", bev=0.03)
    sphere("pelvis", 0.12, (0, 0.0, 0.12), "bone_dk").scale = (1.2, 0.9, 0.6)
    _ribs("cage", 0, 0.04, 0.52, rot=(math.radians(-14), 0, 0), n=4, w=0.15)
    blk("rag_chest", (0.34, 0.20, 0.22), (0, 0.10, 0.44), "rag", bev=0.05)
    blk("collar", (0.36, 0.08, 0.04), (0, 0.02, 0.73), "pal_bone", bev=0.01)
    _skull("skull", 0.04, -0.08, 0.80, tilt=(math.radians(38), 0, math.radians(12)))
    for sx in (-1, 1):
        blk("stain_%d" % sx, (0.06, 0.03, 0.40), (sx * 0.36, 0.15, 1.10), "rust_stain_dk", bev=0)
    return 1.84


def _prisoner_head(x, y, z, tilt):
    """A starved head: grey skin over the bone, sunken eyes, thin grey hair."""
    head = sphere("head", 0.12, (x, y, z), "pris_skin")
    head.scale = (0.9, 1.0, 1.0)
    head.rotation_euler = tilt
    m = Euler(tilt, "XYZ").to_matrix()
    hair = sphere("hair", 0.115, (0, 0, 0), "pris_hair")
    hair.scale = (0.95, 0.9, 0.7)
    off = m @ Vector((0, 0.03, 0.05))
    hair.location = (x + off.x, y + off.y, z + off.z)
    hair.rotation_euler = tilt
    for sx in (-1, 1):
        off = m @ Vector((sx * 0.045, -0.105, 0.01))
        sphere("socket_%d" % sx, 0.03, (x + off.x, y + off.y, z + off.z), "pris_skin_dk")
    off = m @ Vector((0, -0.10, -0.07))
    sphere("beard", 0.06, (x + off.x, y + off.y, z + off.z), "pris_hair").scale = (1.1, 0.8, 0.9)
    return head


def prop_prisoner_slumped():
    """Someone still alive in a cell, only just: sat on the floor against the
    wall in rags, the knees drawn up, the arms round them and the head down on
    them. Skin the grey of Vigil's, and as thin."""
    sphere("hips", 0.15, (0, 0.06, 0.12), "rag").scale = (1.2, 1.0, 0.7)
    torso = blk("torso", (0.30, 0.20, 0.42), (0, 0.10, 0.38), "rag", rot=(math.radians(18), 0, 0), bev=0.07)
    for sx in (-1, 1):
        cyl("thigh_%d" % sx, 0.05, 0.38, (sx * 0.09, -0.08, 0.26), "rag_dk", rot=(math.radians(-55), 0, 0), verts=10)
        cyl("shin_%d" % sx, 0.04, 0.36, (sx * 0.10, -0.30, 0.20), "pris_skin", rot=(math.radians(25), 0, 0), verts=10)
        sphere("foot_%d" % sx, 0.045, (sx * 0.10, -0.38, 0.03), "pris_skin_dk").scale = (0.9, 1.4, 0.6)
        sphere("knee_%d" % sx, 0.055, (sx * 0.09, -0.24, 0.42), "rag_dk")
        # Arms round the knees.
        cyl("arm_%d" % sx, 0.03, 0.32, (sx * 0.17, -0.12, 0.40), "pris_skin", rot=(math.radians(-70), 0, -sx * 0.4),
            verts=8)
    for k in range(4):
        blk("tatter_%d" % k, (0.07, 0.04, 0.12), (-0.12 + k * 0.08, -0.02, 0.10), "rag_dk", bev=0.02)
    _prisoner_head(0, -0.10, 0.56, (math.radians(52), 0, math.radians(8)))
    return 1.12


def prop_prisoner_lying():
    """Someone curled on their side on the floor of a cell, the knees drawn up
    to the chest and the arms in close, in rags, too weak to get up."""
    rot = (math.radians(90), 0, 0)
    blk("torso", (0.40, 0.24, 0.26), (-0.12, 0.0, 0.14), "rag", rot=(0, 0, 0.1), bev=0.08)
    sphere("hips", 0.14, (0.16, 0.02, 0.13), "rag_dk").scale = (1.0, 1.2, 0.9)
    for k, dz in enumerate((0.0, 0.08)):
        cyl("thigh_%d" % k, 0.05, 0.34, (0.24, -0.16, 0.08 + dz), "rag_dk", rot=(math.radians(90), 0, -0.5), verts=10)
        cyl("shin_%d" % k, 0.04, 0.32, (0.44, -0.16, 0.07 + dz), "pris_skin", rot=(math.radians(90), 0, 0.6), verts=10)
        sphere("foot_%d" % k, 0.04, (0.52, -0.02, 0.06 + dz), "pris_skin_dk")
    cyl("arm", 0.03, 0.30, (-0.10, -0.18, 0.16), "pris_skin", rot=(0, math.pi / 2, 0.3), verts=8)
    sphere("hand", 0.04, (0.04, -0.24, 0.15), "pris_skin")
    for k in range(4):
        blk("tatter_%d" % k, (0.10, 0.05, 0.06), (-0.30 + k * 0.14, 0.14, 0.04), "rag_dk", bev=0.02)
    _prisoner_head(-0.42, -0.02, 0.15, (math.radians(10), math.radians(-90), 0))
    _ = rot
    return 1.62


# =================================================================================
#  Scene 10 -- the foyer, the house and its grounds
# =================================================================================
bp.PALETTE.update({
    # His house: slate-blue stone gone green in the damp, dark slate roofs,
    # black iron, dark wood, and the same dull gold as his trim.
    "man_stone":    (0.300, 0.390, 0.420), "man_stone_dk": (0.200, 0.265, 0.300),
    "man_stone_lt": (0.400, 0.505, 0.530), "man_moss":     (0.300, 0.430, 0.360),
    "man_roof":     (0.150, 0.170, 0.230), "man_roof_dk":  (0.095, 0.105, 0.150),
    "man_roof_lt":  (0.220, 0.250, 0.320), "man_trim":     (0.330, 0.500, 0.450),
    "man_window":   (0.060, 0.080, 0.120), "man_lit":      (1.000, 0.800, 0.400),
    "man_wood":     (0.230, 0.150, 0.110), "man_wood_dk":  (0.150, 0.100, 0.075),
    "man_iron":     (0.150, 0.160, 0.190), "man_iron_lt":  (0.300, 0.320, 0.360),
    "man_gold":     (0.700, 0.570, 0.300), "man_void":     (0.030, 0.025, 0.040),
    # The foyer's glass.
    "glass_red":    (0.640, 0.110, 0.130), "glass_blue":   (0.150, 0.240, 0.660),
    "glass_gold":   (0.880, 0.660, 0.220), "glass_violet": (0.440, 0.220, 0.620),
    "glass_lead":   (0.080, 0.075, 0.090), "foyer_stone":  (0.180, 0.170, 0.200),
    "foyer_stone_lt": (0.250, 0.240, 0.280),
    # Dead wood on the hill, gone the colour of the sky over it.
    "dead_bark":    (0.410, 0.470, 0.450), "dead_bark_dk": (0.270, 0.320, 0.310),
    "dead_bark_lt": (0.540, 0.590, 0.560),
})


def limb(name, a, b, r0, r1, colour, verts=8):
    """A tapered length of wood from point a to point b: a branch, a root."""
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cone_add(radius1=r0, radius2=r1, depth=d.length, vertices=verts,
                                    location=(a + b) / 2)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_mode = "QUATERNION"
    ob.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    ob.data.materials.append(material(name, colour))
    return ob


def tip_all(axis, degrees, pivot=(0, 0, 0)):
    """Everything built so far, turned about `axis` through `pivot`: a cart
    tipped onto its side."""
    p = bpy.data.objects.new("tip_pivot", None)
    bpy.context.collection.objects.link(p)
    p.location = pivot
    for ob in list(bpy.context.scene.objects):
        if ob is p or ob.parent is not None or ob.type not in {"MESH", "EMPTY"}:
            continue
        ob.parent = p
        ob.location = ob.location - Vector(pivot)
    e = [0.0, 0.0, 0.0]
    e["XYZ".index(axis)] = math.radians(degrees)
    p.rotation_euler = e
    return p


def crescent(name, cx, y, cz, radius, bead, colour="man_gold", emit=0.0, opening=0.0, crack=True, beads=7):
    """The house's sigil: a crescent of beads open to one side (turned by
    `opening` degrees), with a bead left out for the crack. More beads close
    the arc up where it is drawn small and the beads would stand apart."""
    step = 234.0 / (beads - 1)
    for k in range(beads):
        if crack and k == beads // 2:
            continue
        a = math.radians(62 + k * step + opening)
        sphere("%s_%d" % (name, k), bead, (cx + math.cos(a) * radius, y, cz + math.sin(a) * radius), colour, emit=emit)


def centre(span, elevation=None):
    """Move everything built so far up or down until it sits in the middle of
    the camera's square. The camera aims a third of the way up its frame, and
    anything taller than it is wide runs off the top; make_props.ps1 sits
    every prop back on the bottom of its picture afterwards, so how high it is
    built does not matter -- only that all of it is in the frame."""
    elev = math.radians(bp.CAMERA_ELEVATION if elevation is None else elevation)
    up = Vector((0.0, math.sin(elev), math.cos(elev)))
    bpy.context.view_layer.update()
    lo, hi = float("inf"), float("-inf")
    for ob in bpy.context.scene.objects:
        if ob.type != "MESH":
            continue
        m = ob.matrix_world
        for v in ob.data.vertices:
            h = (m @ v.co).dot(up)
            lo, hi = min(lo, h), max(hi, h)
    dz = (span * 0.34 * math.cos(elev) - (lo + hi) / 2) / math.cos(elev)
    for ob in list(bpy.context.scene.objects):
        if ob.parent is None and ob.type in {"MESH", "EMPTY"}:
            ob.location.z += dz
    if hi - lo > span * 0.97:
        print("  ! %.2f units tall in a %.2f frame" % (hi - lo, span))
    return span


def prop_stained_glass():
    """A tall gothic window in the foyer's dark stone: a pointed lancet of
    glass in deep red, blue, gold and violet between black leading, a stone
    frame round it and a crescent moon worked into the head of the arch. Lit
    from outside, faintly -- enough to throw its colours on the floor."""
    rng = random.Random(31)
    WIN_W, Z0, Z1 = 0.66, 0.70, 3.00            # the straight part of the opening
    APEX = Z1 + 0.62
    bp._coursed("wall", -0.68, 0.68, 0.0, 4.10, 0.06, ("foyer_stone", "foyer_stone_lt", "foyer_stone"), rng,
                gap=lambda cx, z: abs(cx) < 0.42 and Z0 - 0.10 < z < APEX + 0.10)
    blk("backing", (WIN_W + 0.04, 0.04, APEX - Z0 + 0.1), (0, 0.04, (Z0 + APEX) / 2), "glass_lead", bev=0)
    # The panes: three columns of colour, changing up the window, with the
    # leading showing between them.
    colours = ("glass_blue", "glass_red", "glass_gold", "glass_violet")
    pattern = [(1, 0, 1), (0, 2, 0), (3, 1, 3), (0, 2, 0), (1, 3, 1), (2, 0, 2), (3, 1, 3), (0, 2, 0)]
    rows = len(pattern)
    ph = (Z1 - Z0) / rows
    for r_, row in enumerate(pattern):
        for c, ci in enumerate(row):
            blk("pane_%d_%d" % (r_, c), (0.19, 0.04, ph - 0.035), (-0.215 + c * 0.215, 0.0, Z0 + ph * (r_ + 0.5)),
                colours[ci], emit=0.9, rough=0.3, bev=0)
    # The head of the arch: panes stepping in to the point, and the moon.
    for k, (w, z) in enumerate(((0.62, Z1 + 0.10), (0.50, Z1 + 0.26), (0.36, Z1 + 0.40), (0.20, Z1 + 0.52))):
        blk("head_pane_%d" % k, (w, 0.04, 0.13), (0, 0.0, z), "glass_violet" if k % 2 else "glass_blue", emit=0.9,
            rough=0.3, bev=0)
    crescent("moon", -0.02, -0.04, Z1 + 0.28, 0.12, 0.040, colour="glass_gold", emit=1.3)
    # The stone frame: jambs, sill, and the two halves of the pointed arch.
    for sx in (-1, 1):
        blk("jamb_%d" % sx, (0.12, 0.16, Z1 - Z0 + 0.1), (sx * (WIN_W / 2 + 0.06), -0.02, (Z0 + Z1) / 2), "man_stone_dk")
        ln = math.hypot(WIN_W / 2 + 0.06, APEX - Z1)
        ang = math.atan2(APEX - Z1, WIN_W / 2 + 0.06)
        blk("arch_%d" % sx, (ln + 0.10, 0.16, 0.13), (sx * (WIN_W / 4 + 0.03), -0.02, (Z1 + APEX) / 2),
            "man_stone_dk", rot=(0, sx * ang, 0))
    blk("mullion", (0.05, 0.06, Z1 - Z0), (-0.108, -0.03, (Z0 + Z1) / 2), "glass_lead", bev=0)
    blk("mullion_b", (0.05, 0.06, Z1 - Z0), (0.108, -0.03, (Z0 + Z1) / 2), "glass_lead", bev=0)
    blk("sill", (WIN_W + 0.30, 0.24, 0.10), (0, -0.06, Z0 - 0.05), "man_stone_dk")
    return centre(3.5)


def prop_pedestal():
    """A squat plinth of dark stone for a suit of armour to stand on: a base,
    a body, a cap, dull gold banding at top and foot and the cracked moon on
    its face."""
    blk("base", (1.16, 0.70, 0.13), (0, 0, 0.065), "foyer_stone")
    blk("base_trim", (1.18, 0.72, 0.04), (0, 0, 0.14), "man_gold", metal=0.5, rough=0.5)
    blk("body", (0.98, 0.58, 0.26), (0, 0, 0.29), "foyer_stone_lt")
    blk("cap_trim", (1.08, 0.66, 0.04), (0, 0, 0.44), "man_gold", metal=0.5, rough=0.5)
    blk("cap", (1.10, 0.68, 0.09), (0, 0, 0.50), "foyer_stone")
    crescent("moon", -0.02, -0.30, 0.29, 0.07, 0.026, colour="man_gold")
    return 1.375


def prop_mansion_doors():
    """The house's great doors from inside, in the back wall of the foyer:
    two leaves of near-black wood under a pointed arch, strapped with iron and
    studded, a ring on each, and the cracked moon in gold over them."""
    rng = random.Random(37)
    DW, DH, APEX = 1.20, 2.70, 3.55            # half the opening, its straight height, and its point
    bp._coursed("wall", -1.72, 1.72, 0.0, 4.20, 0.08, ("foyer_stone", "foyer_stone_lt", "foyer_stone"), rng,
                gap=lambda cx, z: abs(cx) < DW + 0.16 and z < APEX + 0.12)
    for sx in (-1, 1):
        # The leaf, in planks, iron straps across it with studs, and a ring.
        for k in range(4):
            blk("plank_%d_%d" % (sx, k), (DW / 4 - 0.01, 0.10, DH), (sx * (DW / 8 + k * DW / 4), 0.0, DH / 2),
                "man_wood" if k % 2 else "man_wood_dk", bev=0.01)
        for z in (0.45, 1.35, 2.25):
            blk("strap_%d_%.2f" % (sx, z), (DW - 0.06, 0.04, 0.10), (sx * DW / 2, -0.06, z), "man_iron", metal=0.5)
            for k in range(3):
                sphere("stud_%d_%.2f_%d" % (sx, z, k), 0.025, (sx * (0.18 + k * 0.38), -0.09, z), "man_iron_lt")
        ring("ring_%d" % sx, 0.09, 0.02, (sx * 0.20, -0.10, 1.10), "man_iron_lt", rot=(math.pi / 2, 0, 0))
        # The point of the arch over the leaves: wood, in a frame of stone.
        ln = math.hypot(DW + 0.08, APEX - DH)
        ang = math.atan2(APEX - DH, DW + 0.08)
        blk("arch_%d" % sx, (ln + 0.20, 0.20, 0.22), (sx * (DW + 0.08) / 2, -0.04, (DH + APEX) / 2), "man_stone_dk",
            rot=(0, sx * ang, 0))
        blk("jamb_%d" % sx, (0.22, 0.20, DH), (sx * (DW + 0.11), -0.04, DH / 2), "man_stone_dk")
    blk("tympanum", (DW * 2, 0.06, APEX - DH), (0, 0.04, (DH + APEX) / 2 - 0.05), "man_wood_dk", bev=0)
    blk("meeting", (0.05, 0.12, DH), (0, -0.02, DH / 2), "man_iron", metal=0.5, bev=0)
    crescent("moon", -0.03, -0.06, DH + 0.36, 0.20, 0.06, colour="man_gold")
    blk("step", (DW * 2 + 0.6, 0.40, 0.08), (0, -0.20, 0.04), "man_stone_dk")
    # 128 pixels square at 32 to the unit, cut to 112 wide.
    return centre(4.0)


def _fence_bar(name, x, y, h, colour="man_iron"):
    cyl(name, 0.032, h, (x, y, h / 2), colour, verts=8)
    cone(name + "_tip", 0.065, 0.20, (x, y, h + 0.08), "man_iron_lt", verts=6)
    sphere(name + "_knob", 0.045, (x, y, h - 0.02), colour)


def prop_iron_fence_tall():
    """A length of the house's railing: tall black bars with spear points, two
    rails and a scroll between them, between square posts with finials."""
    n = 8
    W = 1.68
    step = W / n
    for i in range(n):
        _fence_bar("bar_%d" % i, -W / 2 + step * (i + 0.5), 0.0, 2.10)
    for z in (0.22, 1.80):
        blk("rail_%.2f" % z, (W, 0.05, 0.06), (0, 0, z), "man_iron", metal=0.4, bev=0.01)
    for i in range(n - 1):
        ring("scroll_%d" % i, 0.07, 0.016, (-W / 2 + step * (i + 1), 0, 1.62), "man_iron_lt", rot=(math.pi / 2, 0, 0))
    for sx in (-1, 1):
        blk("post_%d" % sx, (0.12, 0.12, 2.30), (sx * (W / 2 + 0.06), 0, 1.15), "man_iron", metal=0.4)
        sphere("finial_%d" % sx, 0.08, (sx * (W / 2 + 0.06), 0, 2.38), "man_iron_lt")
        cone("finial_tip_%d" % sx, 0.05, 0.14, (sx * (W / 2 + 0.06), 0, 2.50), "man_iron_lt", verts=8)
    # 80 pixels square at 32 to the unit, cut to 64 wide by make_props.ps1.
    return centre(2.5)


def prop_iron_fence_tall_v():
    """The same railing running north and south: a cell's length of it seen
    along its run, the bars one behind another and the rails down their side."""
    L = 1.0
    for i in range(4):
        _fence_bar("bar_%d" % i, 0.0, -L / 2 + L / 4 * (i + 0.5), 2.10)
    for z in (0.22, 1.80):
        blk("rail_%.2f" % z, (0.05, L, 0.06), (0, 0, z), "man_iron", metal=0.4, bev=0.01)
    for i in range(3):
        ring("scroll_%d" % i, 0.07, 0.016, (0, -L / 2 + L / 4 * (i + 1), 1.62), "man_iron_lt",
             rot=(math.pi / 2, 0, math.pi / 2))
    # The same scale as iron_fence_tall, cut to 24 wide.
    return centre(2.5)


def _gate_leaf(prefix, w, h, side):
    """One leaf of the gate, hinged at its outer edge (x = 0 here, the leaf
    running out along +x or -x): spear-topped bars rising to the middle, three
    rails, and a scroll on each bay."""
    objs_before = set(bpy.context.scene.objects)
    n = 6
    for i in range(n):
        x = side * (w / n * (i + 0.5))
        rise = 0.16 * (i + 1) / n
        _fence_bar("%sbar_%d" % (prefix, i), x, 0.0, h + rise)
    for z in (0.20, 1.20, h - 0.10):
        blk("%srail_%.2f" % (prefix, z), (w, 0.05, 0.06), (side * w / 2, 0, z), "man_iron", metal=0.4, bev=0.01)
    for i in range(n - 1):
        ring("%sscroll_%d" % (prefix, i), 0.08, 0.016, (side * (w / n * (i + 1)), 0, 0.70), "man_iron_lt",
             rot=(math.pi / 2, 0, 0))
    blk(prefix + "stile", (0.07, 0.06, h + 0.05), (side * 0.035, 0, h / 2), "man_iron", metal=0.4)
    return [ob for ob in bpy.context.scene.objects if ob not in objs_before]


def _mansion_gate(open_):
    """The gate at the foot of the hill: two leaves of wrought iron between
    stone pillars, an iron arch over them with the cracked moon at its top.
    Shut, or swung back into the grounds -- back, so the pillars' feet stay the
    lowest thing in the picture and the two versions line up."""
    PX, LW, LH = 1.36, 1.33, 2.50      # the leaves meet in the middle
    for sx in (-1, 1):
        blk("pillar_%d" % sx, (0.52, 0.52, 2.80), (sx * (PX + 0.26), 0, 1.40), "man_stone")
        blk("pillar_band_%d" % sx, (0.58, 0.58, 0.10), (sx * (PX + 0.26), 0, 0.40), "man_stone_dk")
        blk("pillar_cap_%d" % sx, (0.64, 0.64, 0.14), (sx * (PX + 0.26), 0, 2.87), "man_stone_lt")
        sphere("pillar_ball_%d" % sx, 0.20, (sx * (PX + 0.26), 0, 3.12), "man_stone_lt")
        sphere("moss_%d" % sx, 0.12, (sx * (PX + 0.20), -0.24, 0.35), "man_moss").scale = (1.3, 0.4, 0.8)
    for sx in (-1, 1):
        leaf = _gate_leaf("l%d_" % sx, LW, LH, -sx)
        pivot = bpy.data.objects.new("gate_pivot_%d" % sx, None)
        bpy.context.collection.objects.link(pivot)
        pivot.location = (sx * PX, 0.0, 0.0)
        for ob in leaf:
            ob.parent = pivot
        if open_:
            pivot.rotation_euler = (0, 0, math.radians(-sx * 68))
    # The arch: iron bars bent over from pillar to pillar, scrolls under it, and
    # the moon at its crown.
    for k in range(13):
        a = math.pi * k / 12
        x, z = math.cos(a) * PX, 2.62 + math.sin(a) * 0.86
        sphere("arch_%d" % k, 0.05, (x, 0, z), "man_iron")
        if 0 < k < 12:
            nx, nz = math.cos(math.pi * (k + 1) / 12) * PX, 2.62 + math.sin(math.pi * (k + 1) / 12) * 0.86
            limb("arch_seg_%d" % k, (x, 0, z), (nx, 0, nz), 0.035, 0.035, "man_iron")
    limb("arch_seg_0", (PX, 0, 2.62), (math.cos(math.pi / 12) * PX, 0, 2.62 + math.sin(math.pi / 12) * 0.86),
         0.035, 0.035, "man_iron")
    blk("arch_bar", (PX * 2, 0.05, 0.06), (0, 0, 2.62), "man_iron", metal=0.4, bev=0.01)
    for sx in (-1, 1):
        ring("arch_scroll_%d" % sx, 0.16, 0.02, (sx * 0.55, 0, 2.95), "man_iron_lt", rot=(math.pi / 2, 0, 0))
    crescent("moon", -0.03, -0.03, 3.64, 0.18, 0.058, colour="man_gold", beads=13)
    return centre(4.0)


def prop_mansion_gate():
    return _mansion_gate(False)


def prop_mansion_gate_open():
    return _mansion_gate(True)


def _dead_tree(seed, height, spread):
    """A dead tree grown from a seed: a trunk that twists as it climbs, split
    into limbs that fork and crook and end in hooks, roots knotted into the
    ground. Grey-teal bark, three shades, no leaf anywhere."""
    rng = random.Random(seed)
    tones = ("dead_bark", "dead_bark_dk", "dead_bark_lt")

    def grow(name, base, direction, length, r, depth):
        d = direction.normalized()
        pts = [base]
        # Each limb in three crooked lengths, bending a little each time.
        for k in range(3):
            d = (d + Vector((rng.uniform(-0.45, 0.45), rng.uniform(-0.25, 0.25), rng.uniform(-0.1, 0.25)))).normalized()
            pts.append(pts[-1] + d * (length / 3))
        for k in range(3):
            r0 = r * (1.0 - k * 0.22)
            limb("%s_%d" % (name, k), pts[k], pts[k + 1], r0, r0 * 0.78, tones[(depth + k) % 3] if depth else "dead_bark",
                 verts=8 if depth < 2 else 6)
        if depth >= 3 or r < 0.03:
            # A hooked tip.
            hook = (d + Vector((0, 0, -0.9))).normalized()
            limb(name + "_hook", pts[-1], pts[-1] + hook * length * 0.18, r * 0.4, 0.008, "dead_bark_dk", verts=5)
            return
        # Forks off the last two lengths, out and up.
        for j in range(2):
            a = rng.uniform(0, math.tau)
            nd = Vector((math.cos(a) * spread, math.sin(a) * spread * 0.6, 0.9)) + d * 0.6
            grow("%s_%d_b" % (name, j), pts[2 + j], nd, length * rng.uniform(0.55, 0.72), r * 0.60, depth + 1)

    # The trunk: in four twisting lengths, thick at the foot.
    pts = [Vector((0, 0, 0))]
    d = Vector((0, 0, 1))
    h = height * 0.52
    for k in range(4):
        d = (d + Vector((rng.uniform(-0.35, 0.35), rng.uniform(-0.15, 0.15), 0))).normalized()
        pts.append(pts[-1] + d * (h / 4))
    for k in range(4):
        r0 = 0.24 * (1.0 - k * 0.16)
        limb("trunk_%d" % k, pts[k], pts[k + 1], r0, r0 * 0.84, "dead_bark", verts=10)
        # A dark knot on the face of it.
        if k in (1, 2):
            sphere("knot_%d" % k, r0 * 0.38, (pts[k].x + 0.02, pts[k].y - r0 * 0.85, pts[k].z + 0.10), "dead_bark_dk")
    for j in range(3):
        a = j / 3 * math.tau + rng.uniform(-0.3, 0.3)
        nd = Vector((math.cos(a) * spread, math.sin(a) * spread * 0.5, 1.0))
        grow("limb_%d" % j, pts[3 - (j % 2)], nd, height * 0.42, 0.13, 1)
    # Roots knotted into the ground.
    for k in range(5):
        a = k / 5 * math.tau + 0.3
        limb("root_%d" % k, (0, 0, 0.18), (math.cos(a) * 0.55, math.sin(a) * 0.40, 0.0), 0.12, 0.03, "dead_bark_dk",
             verts=7)


def prop_dead_tree_twisted():
    _dead_tree(41, 3.85, 0.95)
    return centre(4.0)


def prop_dead_tree_twisted_b():
    _dead_tree(57, 3.0, 0.80)
    return centre(3.5)


def prop_cart_overturned():
    """The handcart from the road, or one like it, lying on its side by the
    path up to the house: one wheel still on it and turned up to the sky, the
    other off and lying in the grass, and a board broken out of the bed."""
    BW, Z = _cart_bed("c_", broken=True)
    _wheel("wheel_on", BW / 2 + 0.14, 0.10, 0.42, 0.42)
    tip_all("Y", -96, pivot=(-BW / 2 - 0.06, 0.0, 0.0))
    p = [ob for ob in bpy.context.scene.objects if ob.name == "tip_pivot"][0]
    p.location = (0.20, 0.10, 0.06)
    # Turned so the boards of its floor, stood on edge now, face the camera,
    # a little skew: the wheel still on it lies flat on top, the shafts off to
    # one side. Turned the other way it was its underside, and a brown lump.
    q = bpy.data.objects.new("turn_pivot", None)
    bpy.context.collection.objects.link(q)
    p.parent = q
    q.rotation_euler = (0, 0, math.radians(66))
    _wheel("wheel_off", -0.78, -0.62, 0.05, 0.40, rot=(math.radians(10), 0, math.radians(20)))
    blk("broken_board", (0.12, 0.70, 0.04), (0.80, -0.62, 0.03), "cart_board_lt", rot=(0, 0, math.radians(38)))
    blk("broken_end", (0.12, 0.16, 0.04), (1.05, -0.30, 0.03), "cart_board_dk", rot=(0, 0, math.radians(70)))
    return 2.55


# --- the house itself ----------------------------------------------------------------
# Which top-floor windows are lit, and the one he stands in.
LIT = (-2.0, 1.0)
WATCH = 1.0


def _gothic_window(name, x, y, z, w, h, lit=False, mullion=True):
    """A tall narrow window with a pointed head: a teal stone surround, the
    glass -- dark, or lit warm from inside -- a mullion, and a sill."""
    glass = "man_lit" if lit else "man_window"
    glow = 1.1 if lit else 0.0
    blk(name + "_surround", (w + 0.16, 0.10, h + 0.10), (x, y, z), "man_trim")
    blk(name + "_glass", (w, 0.06, h), (x, y - 0.03, z), glass, emit=glow, rough=0.3, bev=0)
    cone(name + "_head", (w + 0.16) * 0.74, 0.44, (x, y, z + h / 2 + 0.19), "man_trim", verts=4,
         rot=(0, 0, math.radians(45)))
    cone(name + "_head_glass", w * 0.72, 0.30, (x, y - 0.035, z + h / 2 + 0.13), glass, verts=4,
         rot=(0, 0, math.radians(45)), emit=glow)
    if mullion:
        blk(name + "_mullion", (0.04, 0.05, h), (x, y - 0.06, z), "man_trim", bev=0)
    blk(name + "_sill", (w + 0.26, 0.16, 0.06), (x, y - 0.06, z - h / 2 - 0.05), "man_stone_lt")


def _gable_end(name, x, y, width, eave, rise, colour):
    """The triangle of wall under a gable facing the front, in courses."""
    n = 8
    for k in range(n):
        f = 1.0 - (k + 0.5) / n
        blk("%s_%d" % (name, k), (width * f, 0.12, rise / n + 0.01), (x, y, eave + rise * (k + 0.5) / n), colour, bev=0.01)


def _front_roof(name, x, width, depth, eave, pitch, colour, y=0.0):
    """A steep roof whose ridge runs front to back, so its gable faces the
    camera: two slabs leaning on each other."""
    p = math.radians(pitch)
    half = width / 2 + 0.18
    slab = half / math.cos(p)
    rise = half * math.tan(p)
    for side in (-1, 1):
        # The slope turned from the light is a shade lighter, or it renders as
        # a hole in the roof.
        blk("%s_slab_%d" % (name, side), (slab, depth + 0.30, 0.12), (x + side * half / 2, y, eave + rise / 2),
            colour if side < 0 else "man_roof_lt", rot=(0, side * p, 0), bev=0.02)
    blk(name + "_ridge", (0.14, depth + 0.34, 0.12), (x, y, eave + rise + 0.03), "man_roof_dk", bev=0.02)
    return rise


def _manor(watcher=False):
    """His house, from the foot of its hill: tall and narrow and three storeys
    of slate-blue stone gone green in the wet, every window tall and pointed
    and dark but two at the top; steep slate roofs; a lower wing on the left
    with its gable to the front and a round turret on the right corner under a
    witch's-hat roof; one tower out of the middle of the roof, built crooked
    and finished crooked, with his cracked moon on the point of its spire; and
    the door in the middle at the top of a short flight of steps. Nothing on
    it is a face."""
    rng = random.Random(61)
    W, D = 5.2, 2.8
    G, U = 1.95, 1.45
    BASE = 0.30
    H = BASE + G + 2 * U
    fy = -D / 2
    blk("plinth", (W + 0.24, D + 0.24, BASE), (0, 0, BASE / 2), "man_stone_dk")
    blk("core", (W - 0.04, D - 0.10, H - BASE), (0, 0.04, (H + BASE) / 2), "man_stone_dk", bev=0)

    ground = (-2.0, -1.0, 1.0, 2.0)
    upper = (-2.0, -1.0, 0.0, 1.0, 2.0)
    zg, z1, z2 = BASE + 0.45 + 0.70, BASE + G + 0.72, BASE + G + U + 0.72

    def gap(cx, z):
        if abs(cx) < 0.62 and z < BASE + 2.15:
            return True
        for wx in ground:
            if abs(cx - wx) < 0.30 and zg - 0.62 < z < zg + 0.80:
                return True
        for zz in (z1, z2):
            for wx in upper:
                if abs(cx - wx) < 0.29 and zz - 0.55 < z < zz + 0.70:
                    return True
        return False
    bp._coursed("front", -W / 2, W / 2, BASE, H, fy - 0.01, ("man_stone", "man_stone_lt", "man_stone", "man_moss"),
                rng, gap=gap)
    for wx in ground:
        _gothic_window("g_%.1f" % wx, wx, fy - 0.05, zg, 0.40, 0.95)
    for wx in upper:
        _gothic_window("f_%.1f" % wx, wx, fy - 0.05, z1, 0.38, 0.85)
        lit = wx in LIT
        _gothic_window("t_%.1f" % wx, wx, fy - 0.05, z2, 0.38, 0.85, lit=lit, mullion=not lit)
    # String courses between the storeys, broken over the door, and a cornice.
    for k, z in enumerate((BASE + G, BASE + G + U)):
        for sx in (-1, 1):
            blk("string_%d_%d" % (k, sx), (W / 2 - (0.66 if k == 0 else 0.0) + 0.05, 0.12, 0.08),
                (sx * (W / 4 + (0.33 if k == 0 else 0.0)), fy - 0.06, z), "man_trim")
    blk("cornice", (W + 0.18, 0.18, 0.12), (0, fy - 0.07, H), "man_trim")
    for sx in (-1, 1):
        cone("pinnacle_%d" % sx, 0.12, 0.60, (sx * (W / 2 + 0.02), fy - 0.02, H + 0.30), "man_stone_lt", verts=4)

    # The door, raised: a pointed arch of teal stone, two leaves of dark wood,
    # a little gable of its own over it, and the steps up to it.
    DZ, DW, DH = BASE + 0.45, 0.92, 1.25
    blk("door_surround", (DW + 0.30, 0.12, DH + 0.10), (0, fy - 0.06, DZ + DH / 2), "man_trim")
    for sx in (-1, 1):
        blk("door_leaf_%d" % sx, (DW / 2 - 0.02, 0.06, DH), (sx * DW / 4, fy - 0.10, DZ + DH / 2),
            "man_wood" if sx < 0 else "man_wood_dk", bev=0.01)
        sphere("door_ring_%d" % sx, 0.04, (sx * 0.10, fy - 0.15, DZ + DH * 0.45), "man_iron_lt")
    cone("door_head", (DW + 0.30) * 0.74, 0.62, (0, fy - 0.06, DZ + DH + 0.27), "man_trim", verts=4,
         rot=(0, 0, math.radians(45)))
    cone("door_head_wood", DW * 0.74, 0.46, (0, fy - 0.09, DZ + DH + 0.19), "man_wood_dk", verts=4,
         rot=(0, 0, math.radians(45)))
    crescent("door_moon", -0.02, fy - 0.16, DZ + DH + 0.36, 0.11, 0.035, colour="man_gold")
    for k in range(3):
        # Pale, and shallow in the drop between them: real stair depth seen from
        # above is a black hole.
        blk("step_%d" % k, (DW + 0.70 - k * 0.12, 0.30, BASE + 0.15 * (k + 1)),
            (0, fy - 0.92 + k * 0.26, (BASE + 0.15 * (k + 1)) / 2), "man_stone_lt" if k % 2 else "man_stone")
    for sx in (-1, 1):
        blk("cheek_%d" % sx, (0.14, 0.86, BASE + 0.55), (sx * (DW / 2 + 0.42), fy - 0.48, (BASE + 0.55) / 2),
            "man_stone_dk")
        sphere("cheek_ball_%d" % sx, 0.10, (sx * (DW / 2 + 0.42), fy - 0.86, BASE + 0.62), "man_stone_lt")

    # The main roof, steep, iron cresting along its ridge, two tall chimneys
    # on its front slope (a chimney on the back slope floats: see the skill).
    rise = bp.gable_roof("roof", W + 0.10, D, H, 58, "man_roof", overhang=0.24)
    pitch = math.radians(58)
    half = D / 2 + 0.24
    for k in range(1, 6):
        t = k / 6.0
        blk("course_%d" % k, (W + 0.56, 0.05, 0.05), (0, -half * (1 - t), H + rise * t + 0.07), "man_roof_dk",
            rot=(pitch, 0, 0), bev=0)
    # gable_roof caps the ridge in shingle brown; this house's is slate.
    blk("ridge_cap", (W + 0.66, 0.20, 0.14), (0, 0, H + rise + 0.05), "man_roof_dk", bev=0.02)
    for k in range(13):
        x = -W / 2 + 0.25 + k * (W - 0.5) / 12
        cone("crest_%d" % k, 0.04, 0.22, (x, 0.0, H + rise + 0.20), "man_iron", verts=4)
    for sx in (-1, 1):
        cx = sx * 1.75
        cz0 = H + rise * 0.35
        blk("chimney_%d" % sx, (0.40, 0.40, rise * 0.65 + 0.9), (cx, -0.35, cz0 + (rise * 0.65 + 0.9) / 2), "man_stone")
        blk("chimney_cap_%d" % sx, (0.52, 0.52, 0.10), (cx, -0.35, cz0 + rise * 0.65 + 0.95), "man_stone_lt")
        for j in (-1, 1):
            blk("pot_%d_%d" % (sx, j), (0.12, 0.12, 0.22), (cx + j * 0.10, -0.35, cz0 + rise * 0.65 + 1.10), "clay")

    # The tower, out of the middle of the roof, leaning one way, and its spire
    # leaning back the other: built crooked and finished crooked.
    lean, spire_lean = math.radians(8), math.radians(-14)
    tz0, th = H + rise * 0.30, 2.70
    top = Vector((th * math.sin(lean), 0.0, tz0 + th * math.cos(lean)))
    mid = Vector((0, 0.0, tz0)) + (top - Vector((0, 0.0, tz0))) / 2
    blk("tower", (1.15, 1.15, th), tuple(mid), "man_stone", rot=(0, lean, 0))
    for k, f in enumerate((0.30, 0.94)):
        p = Vector((0, 0, tz0)) + (top - Vector((0, 0, tz0))) * f
        blk("tower_band_%d" % k, (1.25, 1.25, 0.10), tuple(p), "man_trim", rot=(0, lean, 0))
    wpos = Vector((0, 0, tz0)) + (top - Vector((0, 0, tz0))) * 0.62 + Vector((0, -0.60, 0))
    blk("tower_win_surround", (0.40, 0.08, 0.86), tuple(wpos), "man_trim", rot=(0, lean, 0))
    blk("tower_win", (0.26, 0.08, 0.72), tuple(wpos + Vector((0, -0.02, 0))), "man_window", rot=(0, lean, 0), bev=0)
    for sx in (-1, 1):
        for sy in (-1, 1):
            p = top + Vector((sx * 0.55, sy * 0.55, 0.12))
            cone("tower_pin_%d_%d" % (sx, sy), 0.08, 0.40, tuple(p + Vector((0, 0, 0.20))), "man_stone_lt", verts=4)
    sh = 1.95
    spire_axis = Vector((math.sin(lean + spire_lean), 0, math.cos(lean + spire_lean)))
    cone("spire", 0.86, sh, tuple(top + spire_axis * (sh / 2)), "man_roof", verts=4,
         rot=(0, lean + spire_lean, math.radians(45)))
    tip = top + spire_axis * sh
    cyl("finial_rod", 0.03, 0.40, tuple(tip + Vector((0, 0, 0.12))), "man_iron", verts=6)
    # A whole crescent on the spire, open to one side: split, at this size it
    # was a pair of horns.
    crescent("finial_moon", tip.x - 0.05, -0.02, tip.z + 0.50, 0.26, 0.075, colour="man_gold", crack=False)

    # The left wing: lower, two storeys, its gable to the front.
    LX, LW, LD = -(W / 2 + 0.90), 1.80, 2.40
    LH = BASE + G + U
    blk("wing_core", (LW, LD, LH - BASE), (LX, 0.20, (LH + BASE) / 2), "man_stone_dk", bev=0)
    lfy = 0.20 - LD / 2
    bp._coursed("wing_front", LX - LW / 2, LX + LW / 2, BASE, LH, lfy - 0.01,
                ("man_stone", "man_stone_lt", "man_moss"), rng,
                gap=lambda cx, z: abs(cx - LX) < 0.30 and (zg - 0.62 < z < zg + 0.80 or z1 - 0.55 < z < z1 + 0.70))
    blk("wing_plinth", (LW + 0.2, LD + 0.2, BASE), (LX, 0.20, BASE / 2), "man_stone_dk")
    _gothic_window("w_g", LX, lfy - 0.05, zg, 0.40, 0.95)
    _gothic_window("w_f", LX, lfy - 0.05, z1, 0.38, 0.85)
    wrise = _front_roof("wing_roof", LX, LW, LD, LH, 56, "man_roof", y=0.20)
    _gable_end("wing_gable", LX, lfy - 0.02, LW, LH, wrise * 0.92, "man_stone")
    _gothic_window("w_attic", LX, lfy - 0.08, LH + wrise * 0.32, 0.26, 0.50)

    # The turret on the right corner: round, a storey taller than the house,
    # under a tall cone of slate.
    TX, TR = W / 2 + 0.55, 0.78
    TH = H + 1.0
    cyl("turret", TR, TH - BASE, (TX, fy + 0.55, (TH + BASE) / 2), "man_stone", verts=20)
    cyl("turret_base", TR + 0.08, BASE + 0.10, (TX, fy + 0.55, (BASE + 0.10) / 2), "man_stone_dk", verts=20)
    for z in (BASE + G, BASE + G + U, TH - 0.06):
        cyl("turret_band_%.1f" % z, TR + 0.05, 0.09, (TX, fy + 0.55, z), "man_trim", verts=20)
    for z, h in ((zg, 0.90), (z1, 0.80), (z2, 0.80), (H + 0.48, 0.60)):
        _gothic_window("tu_%.1f" % z, TX - 0.10, fy + 0.55 - TR - 0.01, z, 0.30, h)
    cone("turret_roof", TR + 0.20, 2.70, (TX, fy + 0.55, TH + 1.35), "man_roof", verts=20)
    cyl("turret_finial", 0.025, 0.40, (TX, fy + 0.55, TH + 2.85), "man_iron", verts=6)

    if watcher:
        # A tall hooded figure standing in the lit window: a dark shape against
        # the warm glass, the point of the hood, the shoulders, the robe to the
        # sill. In front of the glass, which is all light.
        # Straight down from the shoulders and a little wider at the sill: a
        # cone from the sill up read as an hourglass in twelve pixels of glass.
        wx, wy, wz = WATCH, fy - 0.05 - 0.07, z2
        figure = "man_void"
        # Narrow enough that light shows either side of him in twelve pixels
        # of glass, and the point of the hood up against the head of the arch.
        limb("watcher_robe", (wx, wy, wz - 0.44), (wx, wy, wz + 0.06), 0.105, 0.078, figure, verts=12)
        sphere("watcher_shoulders", 0.085, (wx, wy, wz + 0.06), figure).scale = (1.15, 0.5, 0.55)
        sphere("watcher_hood", 0.058, (wx, wy, wz + 0.17), figure).scale = (1.0, 0.6, 1.25)
        cone("watcher_point", 0.046, 0.20, (wx + 0.010, wy, wz + 0.31), figure, verts=8, rot=(0, math.radians(8), 0))
    return centre(11.0, BUILDING), BUILDING


def prop_mansion_facade():
    return _manor(False)


def prop_window_silhouette():
    """The facade again, with him standing in the right-hand lit window, cut
    down by make_props.ps1 ($REGION) to that window alone: an overlay the game
    lays over the facade at the same place, so it matches to the pixel."""
    return _manor(True)


PROPS = {
    "handcart":           (prop_handcart, 64),
    "stall_closed":       (prop_stall_closed, 80),
    "bed_sleeper_man":    (prop_bed_sleeper_man, 64),
    "bed_sleeper_woman":  (prop_bed_sleeper_woman, 64),
    "bed_sleeper_elder":  (prop_bed_sleeper_elder, 64),
    "window_curtained":   (prop_window_curtained, 48),
    "papers_scattered":   (prop_papers_scattered, 48),
    "teacup":             (prop_teacup, 12),
    "cell_door_open":     (prop_cell_door_open, 100),
    "cell_door_rotted":   (prop_cell_door_rotted, 84),
    "mattress":           (prop_mattress, 40),
    "wall_chains":        (prop_wall_chains, 48),
    "skeleton_remains":   (prop_skeleton_remains, 48),
    "skeleton_chained":   (prop_skeleton_chained, 56),
    "prisoner_slumped":   (prop_prisoner_slumped, 40),
    "prisoner_lying":     (prop_prisoner_lying, 48),
    "stained_glass":      (prop_stained_glass, 112),
    "pedestal":           (prop_pedestal, 44),
    "mansion_doors":      (prop_mansion_doors, 128),
    "iron_fence_tall":    (prop_iron_fence_tall, 80),
    "iron_fence_tall_v":  (prop_iron_fence_tall_v, 80),
    "mansion_gate":       (prop_mansion_gate, 128),
    "mansion_gate_open":  (prop_mansion_gate_open, 128),
    "dead_tree_twisted":  (prop_dead_tree_twisted, 128),
    "dead_tree_twisted_b": (prop_dead_tree_twisted_b, 112),
    "cart_overturned":    (prop_cart_overturned, 72),
    "mansion_facade":     (prop_mansion_facade, 352),
    "window_silhouette":  (prop_window_silhouette, 352),
}
