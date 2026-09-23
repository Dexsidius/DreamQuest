# =============================================================================
#  blender_palace.py - the Brimstone Palace: its front and towers, the bridge
#  over its moat, and what stands in its halls.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only palace_keep,drawbridge
#
#  Built with blender_props.py's own tools -- blk, cyl, cone, the palette and
#  the cameras -- and registered into its PROPS table. That file hands itself
#  to this one (see where PROPS is assembled there) rather than being imported
#  by name: run by Blender it is __main__, and importing it would run it twice.
#
#  One Blender unit is about one 32px cell of the map, as everywhere else.
# =============================================================================

import math

import bpy  # noqa: F401  (the parts are Blender objects)

import blender_props as bp

blk, cyl, cone, sphere, material = bp.blk, bp.cyl, bp.cone, bp.sphere, bp.material
BUILDING = bp.BUILDING_ELEVATION
TOP_DOWN = 90.0          # straight down: for what lies flat on the ground

bp.PALETTE.update({
    # Black basalt, a shade lighter where it is dressed, and gold and crimson
    # for the trim; the glow is the fire the palace stands on.
    "pal_stone":      (0.170, 0.140, 0.170),
    "pal_stone_dk":   (0.100, 0.080, 0.100),
    "pal_stone_lt":   (0.280, 0.240, 0.280),
    "pal_trim":       (0.380, 0.320, 0.360),
    "pal_slate":      (0.140, 0.110, 0.150),
    "pal_gold":       (0.820, 0.620, 0.250),
    "pal_gold_dk":    (0.540, 0.380, 0.150),
    "pal_crimson":    (0.520, 0.070, 0.100),
    "pal_crimson_dk": (0.300, 0.040, 0.070),
    "pal_glow":       (1.000, 0.440, 0.150),
    "pal_lava":       (1.000, 0.360, 0.080),
    "pal_step":       (0.420, 0.400, 0.440),
    "pal_step_dk":    (0.300, 0.280, 0.320),
    "pal_iron":       (0.220, 0.220, 0.250),
    "pal_iron_lt":    (0.380, 0.380, 0.420),
    "pal_wood":       (0.340, 0.200, 0.120),
    "pal_wood_dk":    (0.220, 0.130, 0.080),
    "pal_wood_lt":    (0.450, 0.290, 0.170),
    "pal_bone":       (0.880, 0.840, 0.740),
    "pal_void":       (0.060, 0.025, 0.035),
    "pal_char":       (0.120, 0.090, 0.080),
    "bridge_wood":    (0.560, 0.330, 0.180),
    "bridge_wood_lt": (0.640, 0.420, 0.240),
    "bridge_wood_dk": (0.470, 0.240, 0.140),
})


# --- pieces -------------------------------------------------------------------
def lancet(name, x, y, z, w, h, emit=1.3):
    """A tall pointed window lit from inside: a frame, the glass, and a
    pyramid on top that reads from the front as the point of the arch."""
    blk(name + "_frame", (w + 0.18, 0.12, h + 0.12), (x, y, z), "pal_trim")
    blk(name + "_glass", (w, 0.08, h), (x, y - 0.05, z), "pal_glow", emit=emit, bev=0)
    cone(name + "_arch", (w + 0.18) * 0.72, 0.46, (x, y, z + h / 2 + 0.22), "pal_trim", verts=4,
         rot=(0, 0, math.radians(45)))


def spike(name, x, y, z, r, h, colour="pal_stone_lt", tilt=(0.0, 0.0)):
    cone(name, r, h, (x, y, z + h / 2), colour, verts=6, rot=(tilt[0], tilt[1], 0))


def horn(name, x, y, z, side, length=1.6, r=0.22, colour="pal_bone", up=0.55):
    """A horn curving out and up: two cones, the second turned further up."""
    a = side * 0.95
    cone(name + "_a", r, length * 0.6, (x + side * length * 0.22, y, z + length * 0.12), colour, verts=10,
         rot=(0, a, 0))
    cone(name + "_b", r * 0.62, length * 0.55, (x + side * length * 0.42, y, z + length * 0.42), colour,
         verts=10, rot=(0, side * up, 0))


def banner(name, x, y, z, w=0.62, h=2.2):
    """A crimson banner with a gold border at its foot and a horned sigil."""
    blk(name + "_rod", (w + 0.2, 0.08, 0.08), (x, y, z + h / 2 + 0.05), "pal_iron", bev=0)
    blk(name, (w, 0.05, h), (x, y, z), "pal_crimson", bev=0)
    blk(name + "_hem", (w, 0.06, 0.12), (x, y - 0.01, z - h / 2 + 0.06), "pal_gold", bev=0)
    blk(name + "_point", (w * 0.72, 0.05, w * 0.72), (x, y, z - h / 2 - 0.02), "pal_crimson",
        rot=(0, math.radians(45), 0), bev=0)
    sphere(name + "_sigil", w * 0.22, (x, y - 0.05, z + h * 0.18), "pal_gold")
    for s in (-1, 1):
        cone(name + "_sh_%d" % s, w * 0.08, w * 0.45, (x + s * w * 0.2, y - 0.05, z + h * 0.18 + w * 0.2), "pal_gold",
             verts=6, rot=(0, s * 0.6, 0))


def flame(name, x, y, z, r, h, emit=1.6):
    cone(name, r, h, (x, y, z + h / 2), "pal_glow", verts=10, emit=emit)
    cone(name + "_core", r * 0.55, h * 0.7, (x, y - r * 0.2, z + h * 0.35), "pal_gold", verts=8, emit=1.8)


# --- the front -----------------------------------------------------------------
def prop_palace_keep():
    """The palace's front: a long range of black basalt buttressed in the
    same, spikes along every edge and a crimson frieze under them, pointed
    windows lit red from inside, a steep black roof and three spires. In the
    middle the gatehouse stands out from the wall on a flight of grey steps:
    a doorway framed in crimson and, inside that, gold, the doors thrown back
    on red light, and over it a horned skull as wide as the door."""
    W, D, H = 15.0, 3.6, 5.0
    front = -D / 2
    base = 0.6
    blk("plinth", (W + 0.6, D + 0.6, base), (0, 0, base / 2), "pal_stone_dk")
    blk("body", (W, D, H), (0, 0, base + H / 2), "pal_stone", bev=0.03)
    blk("course", (W + 0.1, D + 0.1, 0.16), (0, 0, base + 1.2), "pal_trim", bev=0.02)
    blk("frieze", (W + 0.02, 0.08, 0.34), (0, front - 0.03, base + H - 0.40), "pal_crimson", bev=0)
    blk("frieze_gold", (W + 0.04, 0.09, 0.08), (0, front - 0.04, base + H - 0.20), "pal_gold", bev=0, metal=0.3)
    blk("cornice", (W + 0.3, D + 0.3, 0.18), (0, 0, base + H + 0.09), "pal_trim", bev=0.02)

    # Buttresses down the front, each capped with a spike; not across the gatehouse.
    xs = [-7.1, -5.3, -3.5, 3.5, 5.3, 7.1]
    for k, x in enumerate(xs):
        blk("butt_%d" % k, (0.46, 0.60, H + 0.2), (x, front - 0.22, base + (H + 0.2) / 2), "pal_stone_dk")
        blk("butt_band_%d" % k, (0.52, 0.66, 0.12), (x, front - 0.22, base + H * 0.55), "pal_gold", metal=0.3)
        spike("butt_spike_%d" % k, x, front - 0.22, base + H + 0.2, 0.28, 1.2)
    # Two rows of pointed windows between them.
    for k in range(len(xs) - 1):
        if xs[k] < 0 < xs[k + 1]:
            continue
        x = (xs[k] + xs[k + 1]) / 2
        lancet("win_hi_%d" % k, x, front - 0.02, base + 3.55, 0.46, 1.2)
        lancet("win_lo_%d" % k, x, front - 0.02, base + 1.85, 0.40, 0.8, emit=1.0)
    # Spikes along the parapet.
    for k in range(16):
        x = -W / 2 + 0.45 + k * (W - 0.9) / 15
        if abs(x) < 2.8:
            continue
        spike("parapet_%d" % k, x, front + 0.05, base + H + 0.18, 0.16, 0.62, "pal_stone_dk")

    # The roof: steep black slate, with a ridge of iron.
    rz = base + H + 0.18
    pitch = math.radians(52)
    half = D / 2 + 0.2
    for side in (-1, 1):
        blk("roof_%d" % side, (W - 0.2, half / math.cos(pitch), 0.14),
            (0, side * half / 2, rz + half * math.tan(pitch) / 2), "pal_slate", rot=(-side * pitch, 0, 0), bev=0.02)
    blk("ridge", (W - 0.1, 0.18, 0.16), (0, 0, rz + half * math.tan(pitch) + 0.02), "pal_iron", bev=0.02)
    for k in range(7):
        spike("ridge_spike_%d" % k, -6 + k * 2, 0, rz + half * math.tan(pitch) + 0.05, 0.10, 0.7, "pal_iron")

    # Three spires: a great one behind the gatehouse, a lesser one over each wing.
    for name, x, y, r, h, sh in (("spire_c", 0.0, 0.6, 1.25, 8.2, 3.6),
                                 ("spire_w", -5.0, 0.4, 0.82, 6.6, 2.8),
                                 ("spire_e", 5.0, 0.4, 0.82, 6.6, 2.8)):
        cyl(name, r, h, (x, y, base + h / 2), "pal_stone", verts=16)
        for k in range(3):
            cyl("%s_band_%d" % (name, k), r + 0.05, 0.12, (x, y, base + h * (0.45 + 0.2 * k)), "pal_gold", verts=16,
                metal=0.3)
        lancet(name + "_win", x, y - r + 0.02, base + h - 1.1, 0.34, 0.9)
        for k in range(8):
            a = k / 8 * math.tau
            spike("%s_crown_%d" % (name, k), x + math.cos(a) * r, y + math.sin(a) * r, base + h, 0.14, 0.6,
                  "pal_stone_dk")
        cone(name + "_roof", r + 0.18, sh, (x, y, base + h + sh / 2), "pal_slate", verts=16)
        cyl(name + "_finial", 0.06, 0.8, (x, y, base + h + sh + 0.3), "pal_gold", verts=8, metal=0.4)

    # The gatehouse, standing out of the front.
    gw, gh, gd = 5.4, 6.6, 1.3
    gy = front - gd / 2 + 0.1
    gfront = gy - gd / 2
    blk("gate", (gw, gd, gh), (0, gy, base + gh / 2), "pal_stone_lt", bev=0.03)
    blk("gate_plinth", (gw + 0.3, gd + 0.3, base), (0, gy, base / 2), "pal_stone_dk")
    for s in (-1, 1):
        blk("gate_pier_%d" % s, (0.56, gd + 0.24, gh + 0.4), (s * (gw / 2 - 0.1), gy, base + (gh + 0.4) / 2),
            "pal_stone_dk", bev=0.03)
        spike("gate_pier_spike_%d" % s, s * (gw / 2 - 0.1), gy, base + gh + 0.4, 0.32, 1.5)
        horn("gate_horn_%d" % s, s * 1.2, gfront + 0.2, base + gh + 0.2, s, length=2.2, r=0.26)
    # Its gable: a steep, narrow point over the door, faced in the lighter
    # stone with a gold edge, and a slate roof behind it.
    cone("gate_roof", gw * 0.40, 3.2, (0, gy + 0.2, base + gh + 1.5), "pal_slate", verts=4,
         rot=(0, 0, math.radians(45)))
    blk("gable_face", (2.3, 0.3, 2.3), (0, gfront + 0.12, base + gh), "pal_stone_lt", rot=(0, math.radians(45), 0),
        bev=0.02)
    blk("gable_gold", (2.45, 0.28, 2.45), (0, gfront + 0.2, base + gh), "pal_gold", rot=(0, math.radians(45), 0),
        metal=0.3, bev=0)
    spike("gable_spike", 0, gfront + 0.12, base + gh + 1.55, 0.16, 1.1, "pal_gold")
    # The doorway: crimson outside, gold inside, the doors thrown back on red.
    dz = base + 1.75
    blk("door_red", (3.0, 0.14, 3.7), (0, gfront - 0.05, dz + 0.05), "pal_crimson", bev=0.02)
    # The point of the arch: a crimson diamond half sunk into the frame's head.
    blk("door_red_arch", (2.1, 0.14, 2.1), (0, gfront - 0.05, dz + 1.9), "pal_crimson",
        rot=(0, math.radians(45), 0), bev=0.02)
    blk("door_gold", (2.4, 0.16, 3.3), (0, gfront - 0.07, dz), "pal_gold", metal=0.3, rough=0.5, bev=0.02)
    blk("door_void", (1.9, 0.18, 3.0), (0, gfront - 0.08, dz - 0.1), "pal_crimson_dk", emit=0.55, bev=0)
    blk("door_glow", (1.5, 0.19, 0.5), (0, gfront - 0.09, dz - 1.35), "pal_glow", emit=1.0, bev=0)
    for s in (-1, 1):
        blk("leaf_%d" % s, (0.14, 0.95, 2.9), (s * 0.88, gfront - 0.42, dz - 0.15), "pal_wood_dk",
            rot=(0, 0, s * 0.35))
        for k in range(3):
            blk("leaf_band_%d_%d" % (s, k), (0.16, 0.97, 0.10), (s * 0.88, gfront - 0.42, dz - 1.2 + k * 1.0),
                "pal_iron", rot=(0, 0, s * 0.35), bev=0)
    # Over the door, the skull of something with horns.
    sz = dz + 2.75
    sphere("skull", 0.62, (0, gfront - 0.28, sz), "pal_bone")
    blk("skull_jaw", (0.70, 0.40, 0.34), (0, gfront - 0.42, sz - 0.52), "pal_bone")
    for s in (-1, 1):
        sphere("socket_%d" % s, 0.17, (s * 0.24, gfront - 0.80, sz + 0.06), "pal_glow", emit=1.6)
        horn("skull_horn_%d" % s, s * 0.45, gfront - 0.3, sz + 0.2, s, length=1.7, r=0.2)
    # Banners either side of the door.
    for s in (-1, 1):
        banner("banner_%d" % s, s * 1.95, gfront - 0.06, base + 3.4, w=0.56, h=2.4)
    # The steps: grey, widening toward the foot.
    for k in range(3):
        blk("step_%d" % k, (4.4 - k * 0.5, 0.46, 0.2), (0, gfront - 1.25 + k * 0.42, 0.10 + k * 0.2),
            "pal_step" if k % 2 == 0 else "pal_step_dk", bev=0.02)
    return (17.0, BUILDING)


def prop_palace_tower():
    """A corner tower: a round keep of black stone banded in gold, lit
    windows, a crown of spikes, a black cone of a roof with a gold point, and
    at its foot the red of the lava it stands in."""
    r, h = 1.45, 6.4
    cyl("plinth", r + 0.3, 0.6, (0, 0, 0.3), "pal_stone_dk", verts=24)
    cyl("glow", r + 0.34, 0.12, (0, 0, 0.06), "pal_lava", verts=24, emit=1.2)
    cyl("body", r, h, (0, 0, 0.6 + h / 2), "pal_stone", verts=24)
    for k in range(3):
        cyl("band_%d" % k, r + 0.06, 0.14, (0, 0, 0.6 + 1.6 + k * 1.7), "pal_gold", verts=24, metal=0.3)
    for k, z in enumerate((2.3, 4.2)):
        lancet("win_%d" % k, 0, -r + 0.03, 0.6 + z, 0.40, 0.95)
    for s in (-1, 1):
        lancet("side_win_%d" % s, s * 0.95, -r * 0.72 + 0.02, 0.6 + 3.2, 0.26, 0.7, emit=1.0)
    cyl("parapet", r + 0.24, 0.45, (0, 0, 0.6 + h + 0.22), "pal_trim", verts=24)
    for k in range(10):
        a = k / 10 * math.tau
        spike("crown_%d" % k, math.cos(a) * (r + 0.16), math.sin(a) * (r + 0.16), 0.6 + h + 0.44, 0.16, 0.7,
              "pal_stone_dk")
    cone("roof", r + 0.1, 3.0, (0, 0, 0.6 + h + 0.44 + 1.5), "pal_slate", verts=24)
    for s in (-1, 1):
        horn("roof_horn_%d" % s, s * 0.4, 0, 0.6 + h + 1.2, s, length=1.3, r=0.16, colour="pal_bone")
    cyl("finial", 0.07, 0.9, (0, 0, 0.6 + h + 0.44 + 3.2), "pal_gold", verts=8, metal=0.4)
    banner("banner", 0, -r - 0.05, 0.6 + 3.2 + 0.4, w=0.5, h=1.7)
    return (10.0, BUILDING)


def prop_palace_torch():
    """An iron torch stand as tall as a man, spiked at the foot, with a basket
    of fire on top."""
    for k in range(3):
        a = k / 3 * math.tau
        blk("foot_%d" % k, (0.10, 0.44, 0.08), (math.cos(a) * 0.16, math.sin(a) * 0.16, 0.05), "pal_iron",
            rot=(0, 0, a + math.pi / 2), bev=0)
    cyl("pole", 0.07, 1.3, (0, 0, 0.65), "pal_iron", verts=8)
    cyl("collar", 0.11, 0.10, (0, 0, 0.95), "pal_gold", verts=8, metal=0.3)
    cone("basket", 0.28, 0.34, (0, 0, 1.38), "pal_iron", verts=10, rot=(math.pi, 0, 0))
    flame("fire", 0, 0, 1.46, 0.24, 0.62)
    return 2.6


def prop_drawbridge():
    """The way over the moat, seen from straight above: heavy planks laid
    across, two iron straps and the rivets through them running its length,
    timber rails along both sides, and the ring and chain at the far end that
    raise it. The ends are scorched from the lava either side."""
    L, Wd = 4.0, 3.6
    for k in range(9):
        y = -L / 2 + 0.22 + k * (L - 0.44) / 8
        # Lighter and redder than the palace's own timber: the pixel pass steps
        # colour to a coarse ladder, and a dark brown seen from straight above
        # lands on olive.
        col = ("bridge_wood", "bridge_wood_lt", "bridge_wood_dk")[k % 3]
        blk("plank_%d" % k, (Wd - 0.5, 0.45, 0.08), (0, y, 0.04), col, bev=0.02)
    for s in (-1, 1):
        blk("rail_%d" % s, (0.30, L, 0.20), (s * (Wd / 2 - 0.15), 0, 0.10), "bridge_wood_dk", bev=0.03)
        blk("strap_%d" % s, (0.14, L - 0.1, 0.03), (s * 0.8, 0, 0.095), "pal_iron", bev=0)
        for k in range(8):
            sphere("rivet_%d_%d" % (s, k), 0.045, (s * 0.8, -L / 2 + 0.3 + k * (L - 0.6) / 7, 0.11), "pal_iron_lt")
        blk("ring_%d" % s, (0.30, 0.30, 0.06), (s * (Wd / 2 - 0.15), L / 2 - 0.25, 0.22), "pal_iron", bev=0)
        for k in range(3):
            blk("chain_%d_%d" % (s, k), (0.10, 0.20, 0.06), (s * (Wd / 2 - 0.15), L / 2 - 0.55 - k * 0.26, 0.25),
                "pal_iron_lt", bev=0)
    for s in (-1, 1):
        blk("scorch_%d" % s, (Wd - 0.6, 0.26, 0.01), (0, s * (L / 2 - 0.1), 0.085), "pal_char", bev=0)
    return (4.0, TOP_DOWN)


def prop_lava_bridge():
    """A bridge over one of the streams that run down out of the moat: slabs of
    the palace's basalt laid across, a low parapet down each side and a post
    at each corner, running east and west. Seen from straight above, like the
    drawbridge, because it lies over the lava the way the floor does."""
    L, Wd = 4.0, 2.0
    for k in range(6):
        x = -L / 2 + 0.34 + k * (L - 0.68) / 5
        blk("slab_%d" % k, (0.64, Wd - 0.36, 0.12), (x, 0, 0.06), "pal_trim" if k % 2 else "pal_step", bev=0.02)
    for s in (-1, 1):
        blk("parapet_%d" % s, (L, 0.24, 0.30), (0, s * (Wd / 2 - 0.12), 0.15), "pal_stone_lt", bev=0.03)
        blk("coping_%d" % s, (L, 0.14, 0.05), (0, s * (Wd / 2 - 0.12), 0.32), "pal_step_dk", bev=0)
        for e in (-1, 1):
            blk("post_%d_%d" % (s, e), (0.34, 0.34, 0.46), (e * (L / 2 - 0.17), s * (Wd / 2 - 0.12), 0.23),
                "pal_stone_lt", bev=0.03)
            sphere("knob_%d_%d" % (s, e), 0.09, (e * (L / 2 - 0.17), s * (Wd / 2 - 0.12), 0.5), "pal_gold")
    return (4.0, TOP_DOWN)


# =================================================================================
#  Inside: the foyer
# =================================================================================
def prop_throne_door():
    """The doors at the head of the runner: a pair of them, dark oak banded in
    iron, taller than three men, under a pointed arch of black stone edged in
    gold, with a demon's face in bronze for a knocker on each leaf."""
    W, H = 3.4, 4.2
    for s in (-1, 1):
        blk("jamb_%d" % s, (0.62, 0.7, H + 0.4), (s * (W / 2 + 0.31), 0.0, (H + 0.4) / 2), "pal_stone", bev=0.03)
        blk("jamb_gold_%d" % s, (0.10, 0.72, H + 0.2), (s * (W / 2 + 0.02), -0.02, (H + 0.2) / 2), "pal_gold",
            metal=0.3, bev=0)
        spike("jamb_spike_%d" % s, s * (W / 2 + 0.31), 0, H + 0.4, 0.30, 1.0, "pal_stone_dk")
    cone("arch", W * 0.74, 1.5, (0, 0.0, H + 0.4 + 0.55), "pal_stone", verts=4, rot=(0, 0, math.radians(45)))
    cone("arch_gold", W * 0.58, 1.1, (0, -0.06, H + 0.2 + 0.42), "pal_gold", verts=4, rot=(0, 0, math.radians(45)))
    blk("lintel", (W + 1.3, 0.76, 0.34), (0, 0, H + 0.2), "pal_stone_lt", bev=0.03)
    for s in (-1, 1):
        x = s * W / 4
        blk("leaf_%d" % s, (W / 2 - 0.04, 0.22, H), (x, -0.12, H / 2), "pal_wood", bev=0.02)
        for k in range(4):
            blk("plank_line_%d_%d" % (s, k), (0.05, 0.23, H - 0.2), (x - W / 4 + 0.34 + k * 0.34, -0.13, H / 2),
                "pal_wood_dk", bev=0)
        for k, z in enumerate((0.6, 2.1, 3.6)):
            blk("band_%d_%d" % (s, k), (W / 2 - 0.10, 0.26, 0.16), (x, -0.14, z), "pal_iron", bev=0.01)
            for j in range(3):
                sphere("stud_%d_%d_%d" % (s, k, j), 0.05, (x - 0.5 + j * 0.5, -0.28, z), "pal_iron_lt")
        # The knocker: a bronze face with horns, and the ring in its mouth.
        sphere("face_%d" % s, 0.24, (s * 0.42, -0.30, 2.0), "pal_gold")
        for h in (-1, 1):
            cone("face_horn_%d_%d" % (s, h), 0.06, 0.34, (s * 0.42 + h * 0.2, -0.30, 2.24), "pal_gold", verts=6,
                 rot=(0, h * 0.7, 0))
        for h in (-1, 1):
            sphere("face_eye_%d_%d" % (s, h), 0.05, (s * 0.42 + h * 0.09, -0.52, 2.05), "pal_glow", emit=1.5)
        cyl("ring_%d" % s, 0.18, 0.05, (s * 0.42, -0.36, 1.66), "pal_gold", rot=(math.pi / 2, 0, 0), verts=16)
    blk("threshold", (W + 1.4, 0.9, 0.12), (0, -0.2, 0.06), "pal_step")
    return 7.0


def prop_palace_walkway():
    """One length of the bridges that cross the foyer from balcony to
    balcony overhead: a slab of black stone with a gold-edged face, a rail
    along each side on posts, and a post at each end where the next length
    joins. Laid end to end on the map's overhead layer."""
    L, Dp, T = 2.0, 2.6, 0.45
    blk("slab", (L, Dp, T), (0, 0, T / 2), "pal_stone", bev=0.02)
    blk("fascia", (L, 0.08, T * 0.7), (0, -Dp / 2 - 0.02, T / 2), "pal_crimson", bev=0)
    blk("fascia_gold", (L, 0.09, 0.08), (0, -Dp / 2 - 0.03, T - 0.04), "pal_gold", metal=0.3, bev=0)
    blk("fascia_gold_lo", (L, 0.09, 0.06), (0, -Dp / 2 - 0.03, 0.04), "pal_gold_dk", bev=0)
    for yy in (-Dp / 2 + 0.12, Dp / 2 - 0.12):
        blk("rail_%d" % int(yy * 10), (L, 0.12, 0.10), (0, yy, T + 0.62), "pal_gold_dk", metal=0.3)
        for s in (-1, 1):
            blk("post_%d_%d" % (int(yy * 10), s), (0.16, 0.16, 0.66), (s * (L / 2 - 0.08), yy, T + 0.33), "pal_stone_lt")
            sphere("knob_%d_%d" % (int(yy * 10), s), 0.08, (s * (L / 2 - 0.08), yy, T + 0.72), "pal_gold")
        for k in range(4):
            blk("baluster_%d_%d" % (int(yy * 10), k), (0.06, 0.06, 0.52), (-0.6 + k * 0.4, yy, T + 0.30), "pal_iron")
    # The underside's shadow, so the length reads as something with air under it.
    blk("underside", (L, Dp - 0.2, 0.06), (0, 0.05, 0.0), "pal_stone_dk", bev=0)
    return 3.5


def prop_palace_pillar():
    """A column of black stone on a stepped gold-trimmed base, a crimson band
    at a man's height, and a gilded capital with horns turning off it."""
    blk("base", (0.9, 0.9, 0.24), (0, 0, 0.12), "pal_stone_dk")
    blk("base_gold", (0.78, 0.78, 0.08), (0, 0, 0.28), "pal_gold", metal=0.3, bev=0.01)
    cyl("shaft", 0.30, 3.4, (0, 0, 0.32 + 1.7), "pal_stone", verts=16)
    for k in range(4):
        a = k / 4 * math.tau + math.pi / 4
        cyl("flute_%d" % k, 0.05, 3.3, (math.cos(a) * 0.29, math.sin(a) * 0.29, 0.32 + 1.7), "pal_stone_lt", verts=6)
    cyl("band", 0.33, 0.24, (0, 0, 1.4), "pal_crimson", verts=16)
    cyl("band_gold", 0.34, 0.06, (0, 0, 1.54), "pal_gold", verts=16, metal=0.3)
    blk("capital", (0.86, 0.86, 0.30), (0, 0, 3.87), "pal_gold", metal=0.3, rough=0.5)
    blk("abacus", (1.0, 1.0, 0.14), (0, 0, 4.09), "pal_stone_dk")
    for s in (-1, 1):
        horn("cap_horn_%d" % s, s * 0.3, -0.1, 3.8, s, length=0.7, r=0.09, colour="pal_bone")
    return 4.6


def prop_palace_brazier():
    """A black iron bowl of fire on three clawed legs."""
    for k in range(3):
        a = k / 3 * math.tau + math.pi / 2
        blk("leg_%d" % k, (0.09, 0.09, 0.9), (math.cos(a) * 0.22, math.sin(a) * 0.22, 0.45), "pal_iron",
            rot=(math.sin(a) * 0.25, -math.cos(a) * 0.25, 0))
        cone("claw_%d" % k, 0.07, 0.18, (math.cos(a) * 0.32, math.sin(a) * 0.32, 0.06), "pal_iron", verts=6)
    cyl("bowl", 0.40, 0.26, (0, 0, 0.98), "pal_iron", verts=16)
    cyl("rim", 0.43, 0.06, (0, 0, 1.12), "pal_gold", verts=16, metal=0.3)
    cyl("coals", 0.36, 0.06, (0, 0, 1.12), "pal_lava", verts=16, emit=1.4)
    flame("fire", 0, 0, 1.12, 0.30, 0.62)
    for k in range(3):
        a = k / 3 * math.tau
        flame("tongue_%d" % k, math.cos(a) * 0.18, math.sin(a) * 0.18, 1.12, 0.12, 0.38)
    return 2.2


def prop_palace_banner():
    """A long crimson banner on an iron rod, a gold horned sigil on it."""
    banner("banner", 0, 0, 1.4, w=0.9, h=2.4)
    return 2.8


def prop_demon_statue():
    """A demon of black stone crouched on a plinth, wings folded, horns back,
    watching whoever walks up the runner."""
    blk("plinth", (1.2, 1.2, 0.5), (0, 0, 0.25), "pal_stone_dk")
    blk("plinth_gold", (1.26, 1.26, 0.08), (0, 0, 0.5), "pal_gold", metal=0.3, bev=0.01)
    blk("plinth_top", (1.1, 1.1, 0.14), (0, 0, 0.6), "pal_stone")
    sphere("haunch", 0.42, (0, 0.12, 1.02), "pal_stone_lt")
    sphere("chest", 0.40, (0, -0.10, 1.40), "pal_stone_lt")
    for s in (-1, 1):
        blk("arm_%d" % s, (0.16, 0.16, 0.66), (s * 0.30, -0.26, 1.02), "pal_stone_lt", rot=(0.3, 0, 0))
        cone("claw_%d" % s, 0.09, 0.22, (s * 0.30, -0.40, 0.72), "pal_stone", verts=6, rot=(math.pi, 0, 0))
        blk("wing_%d" % s, (0.14, 0.9, 1.1), (s * 0.40, 0.30, 1.50), "pal_stone", rot=(0.4, s * -0.3, 0))
        cone("wing_tip_%d" % s, 0.10, 0.5, (s * 0.46, 0.55, 2.2), "pal_stone", verts=6)
        blk("foot_%d" % s, (0.22, 0.34, 0.14), (s * 0.22, -0.16, 0.74), "pal_stone_lt")
    sphere("head", 0.26, (0, -0.30, 1.92), "pal_stone_lt")
    blk("snout", (0.22, 0.20, 0.16), (0, -0.52, 1.86), "pal_stone_lt")
    for s in (-1, 1):
        sphere("eye_%d" % s, 0.05, (s * 0.10, -0.54, 1.98), "pal_glow", emit=1.4)
        horn("horn_%d" % s, s * 0.14, -0.26, 2.08, s, length=0.8, r=0.08, colour="pal_stone")
    return 3.0


def prop_palace_sigil():
    """Woven into the runner: a gold ring, a horned skull in it and a flame
    over the skull. Seen from straight above, since it lies on the floor."""
    cyl("ring", 1.35, 0.03, (0, 0, 0.015), "pal_gold", verts=40)
    cyl("ring_in", 1.20, 0.035, (0, 0, 0.02), "pal_crimson_dk", verts=40)
    for k in range(8):
        a = k / 8 * math.tau
        blk("tick_%d" % k, (0.10, 0.28, 0.04), (math.cos(a) * 1.28, math.sin(a) * 1.28, 0.03), "pal_gold",
            rot=(0, 0, a + math.pi / 2), bev=0)
    blk("skull", (0.62, 0.56, 0.04), (0, -0.1, 0.04), "pal_gold", bev=0)
    blk("jaw", (0.40, 0.24, 0.04), (0, -0.46, 0.04), "pal_gold", bev=0)
    for s in (-1, 1):
        blk("socket_%d" % s, (0.14, 0.14, 0.05), (s * 0.14, -0.08, 0.05), "pal_crimson_dk", bev=0)
        blk("horn_%d" % s, (0.14, 0.62, 0.04), (s * 0.48, 0.24, 0.04), "pal_gold", rot=(0, 0, s * 0.7), bev=0)
        blk("horn_tip_%d" % s, (0.10, 0.36, 0.04), (s * 0.70, 0.60, 0.04), "pal_gold", rot=(0, 0, s * -0.1), bev=0)
    blk("flame", (0.22, 0.46, 0.04), (0, 0.52, 0.04), "pal_gold", bev=0)
    return (3.0, TOP_DOWN)


def prop_palace_baluster():
    """One cell's length of the rail along a balcony's edge, running north
    and south: a post with a gold knob, and the rail off it both ways."""
    blk("post", (0.20, 0.20, 0.70), (0, 0, 0.35), "pal_stone_lt")
    sphere("knob", 0.11, (0, 0, 0.76), "pal_gold")
    blk("rail", (0.12, 1.0, 0.10), (0, 0, 0.62), "pal_gold_dk", metal=0.3)
    for y in (-0.33, 0.33):
        blk("bal_%d" % int(y * 10), (0.07, 0.07, 0.52), (0, y, 0.30), "pal_iron")
    return 1.4


# =================================================================================
#  The rooms off it
# =================================================================================
def prop_palace_chandelier():
    """A ring of gold hung on three chains, candles burning all round it.
    Hung overhead: the map lays it on the layer drawn over everyone."""
    cyl("ring", 0.9, 0.12, (0, 0, 1.0), "pal_gold", verts=28, metal=0.4, rough=0.45)
    cyl("ring_in", 0.72, 0.13, (0, 0, 1.0), "pal_iron", verts=28)
    cone("boss", 0.22, 0.4, (0, 0, 0.82), "pal_gold", verts=12, rot=(math.pi, 0, 0))
    for k in range(3):
        a = k / 3 * math.tau
        blk("chain_%d" % k, (0.05, 0.05, 1.6), (math.cos(a) * 0.45, math.sin(a) * 0.45, 1.85), "pal_iron",
            rot=(-math.sin(a) * 0.28, math.cos(a) * 0.28, 0), bev=0)
    for k in range(10):
        a = k / 10 * math.tau
        x, y = math.cos(a) * 0.82, math.sin(a) * 0.82
        cyl("candle_%d" % k, 0.06, 0.26, (x, y, 1.19), "pal_bone", verts=8)
        cone("flame_%d" % k, 0.06, 0.18, (x, y, 1.41), "pal_glow", verts=8, emit=1.8)
    return 2.6


def prop_pipe_organ():
    """The ballroom's organ against the back wall: a black case with spikes
    along its top, three banks of pipes -- tallest in the middle -- gold and
    iron, the console at its foot with its keys and its bench, and a red
    light coming up from inside it."""
    blk("case", (5.2, 1.0, 3.2), (0, 0.3, 1.6), "pal_stone", bev=0.03)
    blk("case_trim", (5.3, 1.05, 0.14), (0, 0.3, 3.2), "pal_gold", metal=0.3)
    blk("glow", (4.4, 0.05, 2.2), (0, -0.22, 1.9), "pal_crimson", emit=0.6, bev=0)
    import random
    rng = random.Random(66)
    for bank, (x0, n, top) in enumerate(((-1.9, 7, 4.0), (-0.72, 9, 5.6), (1.0, 7, 4.0))):
        for k in range(n):
            x = x0 + k * 0.2 - (0.0 if bank != 1 else 0.1)
            h = top - abs(k - (n - 1) / 2) * 0.28 + rng.uniform(-0.05, 0.05)
            col = "pal_gold" if k % 2 == 0 else "pal_iron_lt"
            cyl("pipe_%d_%d" % (bank, k), 0.085, h - 1.0, (x + 0.1, -0.25, 1.0 + (h - 1.0) / 2), col, verts=10,
                metal=0.35, rough=0.5)
            cone("mouth_%d_%d" % (bank, k), 0.09, 0.14, (x + 0.1, -0.33, 1.5), "pal_void", verts=4)
    for k in range(7):
        spike("top_spike_%d" % k, -2.4 + k * 0.8, 0.3, 3.27, 0.12, 0.5, "pal_stone_dk")
    for s in (-1, 1):
        horn("organ_horn_%d" % s, s * 2.5, 0.3, 3.3, s, length=1.1, r=0.12)
    # The console.
    blk("console", (2.2, 0.9, 1.0), (0, -0.8, 0.5), "pal_wood_dk", bev=0.02)
    blk("keys", (1.9, 0.30, 0.06), (0, -1.12, 0.96), "pal_bone", bev=0)
    blk("keys_b", (1.9, 0.14, 0.07), (0, -1.02, 1.02), "pal_void", bev=0)
    blk("bench", (1.4, 0.40, 0.46), (0, -1.66, 0.23), "pal_wood", bev=0.02)
    return 6.4


def prop_palace_mirror():
    """A tall mirror in a heavy gold frame, the glass a smoked violet that
    shows nobody in it."""
    blk("frame", (1.0, 0.16, 2.2), (0, 0, 1.2), "pal_gold", metal=0.35, rough=0.5)
    blk("glass", (0.78, 0.18, 1.94), (0, -0.01, 1.2), (0.46, 0.40, 0.56), rough=0.2)
    cone("crest", 0.5, 0.5, (0, 0, 2.5), "pal_gold", verts=4, rot=(0, 0, math.radians(45)))
    sphere("gem", 0.08, (0, -0.1, 2.4), "pal_crimson")
    blk("foot", (1.1, 0.4, 0.12), (0, 0, 0.06), "pal_stone_dk")
    return 2.8


def prop_palace_table():
    """The banquet table: a long black board on carved legs, a crimson runner
    down it, and dinner laid -- gold candelabra, silver platters, goblets, and
    something roasted in the middle of it."""
    L, Dp = 5.4, 1.3
    blk("top", (L, Dp, 0.14), (0, 0, 0.80), "pal_wood_dk", bev=0.02)
    blk("apron", (L - 0.3, Dp - 0.2, 0.18), (0, 0, 0.66), "pal_wood_dk")
    for sx in (-1, 1):
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.20, 0.20, 0.64), (sx * (L / 2 - 0.3), sy * (Dp / 2 - 0.2), 0.32), "pal_wood")
    blk("runner", (L - 0.6, 0.46, 0.02), (0, 0, 0.88), "pal_crimson", bev=0)
    for k, x in enumerate((-1.9, 0.0, 1.9)):
        cyl("cndl_base_%d" % k, 0.12, 0.08, (x, 0, 0.91), "pal_gold", verts=10, metal=0.3)
        cyl("cndl_stem_%d" % k, 0.04, 0.46, (x, 0, 1.14), "pal_gold", verts=8, metal=0.3)
        blk("cndl_arm_%d" % k, (0.52, 0.06, 0.06), (x, 0, 1.30), "pal_gold", metal=0.3, bev=0)
        for j in (-1, 0, 1):
            cyl("cndl_%d_%d" % (k, j), 0.04, 0.2, (x + j * 0.24, 0, 1.42), "pal_bone", verts=8)
            cone("cndl_fl_%d_%d" % (k, j), 0.04, 0.12, (x + j * 0.24, 0, 1.58), "pal_glow", verts=8, emit=1.8)
    sphere("roast", 0.24, (0.95, 0.0, 1.02), "pal_wood_lt")
    cyl("platter", 0.34, 0.03, (0.95, 0, 0.9), (0.72, 0.72, 0.76), verts=16)
    for k, (x, y) in enumerate(((-2.4, -0.4), (-1.3, 0.42), (-0.7, -0.4), (0.4, 0.42), (1.5, -0.4), (2.4, 0.42))):
        cyl("plate_%d" % k, 0.18, 0.02, (x, y, 0.88), (0.74, 0.74, 0.78), verts=14)
        cyl("goblet_%d" % k, 0.05, 0.22, (x + 0.24, y * 0.6, 0.98), "pal_gold", verts=8, metal=0.35)
        cyl("wine_%d" % k, 0.045, 0.02, (x + 0.24, y * 0.6, 1.09), "pal_crimson_dk", verts=8)
    return 6.0


def prop_palace_hearth():
    """A fireplace a man could stand in, of black marble with a gold mantel, a
    horned skull on the chimney breast, and lava for a fire."""
    blk("breast", (3.0, 0.8, 3.6), (0, 0.4, 1.8), "pal_stone", bev=0.03)
    blk("mantel", (3.3, 1.0, 0.24), (0, 0.2, 2.0), "pal_gold", metal=0.3, rough=0.5)
    for s in (-1, 1):
        blk("cheek_%d" % s, (0.5, 0.9, 1.9), (s * 1.15, 0.1, 0.95), "pal_stone_lt", bev=0.02)
    blk("opening", (1.8, 0.3, 1.5), (0, -0.2, 0.75), "pal_void", bev=0)
    blk("lava", (1.7, 0.6, 0.18), (0, -0.25, 0.1), "pal_lava", emit=1.6, bev=0)
    flame("fire_a", -0.4, -0.3, 0.18, 0.24, 0.9)
    flame("fire_b", 0.3, -0.25, 0.18, 0.30, 1.1)
    flame("fire_c", 0.0, -0.4, 0.18, 0.18, 0.6)
    sphere("skull", 0.34, (0, -0.02, 2.75), "pal_bone")
    for s in (-1, 1):
        sphere("socket_%d" % s, 0.08, (s * 0.12, -0.30, 2.78), "pal_glow", emit=1.5)
        horn("skull_horn_%d" % s, s * 0.25, 0.0, 2.85, s, length=0.9, r=0.1)
    return 4.4


def prop_palace_bed():
    """A four-poster: a black frame, gold at the tops of the posts, a crimson
    canopy and curtains tied back, black sheets and red pillows."""
    L, Wd = 2.2, 1.7
    blk("frame", (Wd, L, 0.36), (0, 0, 0.30), "pal_wood_dk")
    blk("mattress", (Wd - 0.12, L - 0.2, 0.22), (0, 0, 0.58), "pal_void")
    blk("sheet", (Wd - 0.08, L * 0.62, 0.24), (0, -L * 0.16, 0.60), "pal_crimson_dk")
    for s in (-1, 1):
        blk("pillow_%d" % s, (0.62, 0.36, 0.16), (s * 0.38, L / 2 - 0.36, 0.74), "pal_crimson")
    blk("headboard", (Wd, 0.14, 1.2), (0, L / 2 - 0.06, 0.9), "pal_wood_dk")
    sphere("head_gem", 0.1, (0, L / 2 - 0.16, 1.2), "pal_gold")
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * (Wd / 2 - 0.06), sy * (L / 2 - 0.06)
            blk("post_%d_%d" % (sx, sy), (0.12, 0.12, 2.3), (x, y, 1.15), "pal_wood_dk")
            sphere("finial_%d_%d" % (sx, sy), 0.09, (x, y, 2.36), "pal_gold")
            blk("curtain_%d_%d" % (sx, sy), (0.22, 0.16, 1.7), (x - sx * 0.1, y, 1.3), "pal_crimson")
            blk("tie_%d_%d" % (sx, sy), (0.26, 0.18, 0.08), (x - sx * 0.1, y, 1.0), "pal_gold", bev=0)
    # The canopy's frame only, and no cloth over it: from the height the room is
    # seen from, a canopy is a red lid over the bed and hides all of it.
    for sy in (-1, 1):
        blk("tester_%d" % sy, (Wd + 0.1, 0.10, 0.12), (0, sy * (L / 2 - 0.06), 2.26), "pal_wood_dk", bev=0)
        blk("tester_gold_%d" % sy, (Wd + 0.12, 0.11, 0.04), (0, sy * (L / 2 - 0.06), 2.18), "pal_gold", bev=0)
    for sx in (-1, 1):
        blk("tester_side_%d" % sx, (0.10, L + 0.1, 0.12), (sx * (Wd / 2 - 0.06), 0, 2.26), "pal_wood_dk", bev=0)
    return 3.8


def prop_cell_bars():
    """Two metres of the bars across the front of a cell: iron uprights
    between a rail at the foot and one at the head, set in stone at both."""
    blk("sill", (2.0, 0.3, 0.14), (0, 0, 0.07), "pal_stone_dk", bev=0.01)
    blk("head", (2.0, 0.3, 0.2), (0, 0, 2.3), "pal_stone_dk", bev=0.01)
    blk("rail_lo", (2.0, 0.10, 0.08), (0, 0, 0.3), "pal_iron", bev=0)
    blk("rail_hi", (2.0, 0.10, 0.08), (0, 0, 2.08), "pal_iron", bev=0)
    for k in range(7):
        cyl("bar_%d" % k, 0.045, 2.1, (-0.86 + k * 0.287, 0, 1.2), "pal_iron", verts=8)
        cone("tip_%d" % k, 0.05, 0.1, (-0.86 + k * 0.287, 0, 0.2), "pal_iron", verts=6, rot=(math.pi, 0, 0))
    return 2.6


def prop_cell_door():
    """The same bars hung as a door: a heavier frame, hinges, and a lock box."""
    prop_cell_bars()
    blk("frame_l", (0.12, 0.14, 2.0), (-0.9, -0.03, 1.2), "pal_iron_lt", bev=0)
    blk("frame_r", (0.12, 0.14, 2.0), (0.9, -0.03, 1.2), "pal_iron_lt", bev=0)
    blk("brace", (1.8, 0.12, 0.10), (0, -0.03, 1.2), "pal_iron_lt", bev=0)
    blk("lock", (0.26, 0.18, 0.30), (0.7, -0.08, 1.2), "pal_iron", bev=0.01)
    sphere("keyhole", 0.04, (0.7, -0.18, 1.2), "pal_void")
    for z in (0.6, 1.8):
        blk("hinge_%d" % int(z * 10), (0.20, 0.16, 0.12), (-0.96, -0.04, z), "pal_iron", bev=0)
    return 2.6


def prop_wall_shackles():
    """Manacles on chains from two rings in the wall, and dark stains under them."""
    blk("plate", (0.9, 0.08, 0.5), (0, 0.1, 1.5), "pal_iron", bev=0.01)
    for s in (-1, 1):
        cyl("ring_%d" % s, 0.10, 0.04, (s * 0.26, 0.04, 1.5), "pal_iron_lt", rot=(math.pi / 2, 0, 0), verts=12)
        for k in range(4):
            blk("link_%d_%d" % (s, k), (0.06, 0.06, 0.14), (s * (0.26 + k * 0.03), 0.0, 1.36 - k * 0.14),
                "pal_iron_lt", bev=0)
        cyl("cuff_%d" % s, 0.10, 0.10, (s * 0.38, -0.02, 0.8), "pal_iron", verts=12)
    blk("stain", (0.9, 0.04, 0.6), (0, 0.13, 0.5), "pal_crimson_dk", bev=0)
    return 2.0


def prop_torture_rack():
    """A rack: a timber frame on legs, a roller at each end with a spoked
    wheel on it, and the ropes that run between them."""
    L, Wd = 2.4, 0.9
    for sx in (-1, 1):
        blk("rail_%d" % sx, (0.14, L, 0.16), (sx * Wd / 2, 0, 0.72), "pal_wood", bev=0.02)
        for sy in (-1, 1):
            blk("leg_%d_%d" % (sx, sy), (0.14, 0.14, 0.72), (sx * Wd / 2, sy * (L / 2 - 0.1), 0.36), "pal_wood_dk")
    for sy in (-1, 1):
        cyl("roller_%d" % sy, 0.10, Wd + 0.1, (0, sy * (L / 2 - 0.2), 0.86), "pal_wood_dk", rot=(0, math.pi / 2, 0),
            verts=10)
        cyl("wheel_%d" % sy, 0.32, 0.06, (Wd / 2 + 0.12, sy * (L / 2 - 0.2), 0.86), "pal_iron",
            rot=(0, math.pi / 2, 0), verts=14)
        for k in range(4):
            blk("spoke_%d_%d" % (sy, k), (0.05, 0.64, 0.05), (Wd / 2 + 0.14, sy * (L / 2 - 0.2), 0.86), "pal_iron",
                rot=(k * math.pi / 4, 0, 0), bev=0)
    blk("bed", (Wd - 0.1, L - 0.6, 0.06), (0, 0, 0.78), "pal_wood_lt", bev=0)
    for sx in (-1, 1):
        blk("rope_%d" % sx, (0.04, L - 0.4, 0.04), (sx * 0.18, 0, 0.9), (0.72, 0.64, 0.46), bev=0)
    return 3.0


def prop_iron_cage():
    """A round cage on the floor with somebody's bones still in it."""
    cyl("floor", 0.55, 0.08, (0, 0, 0.04), "pal_iron", verts=16)
    cyl("roof", 0.55, 0.08, (0, 0, 1.6), "pal_iron", verts=16)
    cone("cap", 0.35, 0.3, (0, 0, 1.79), "pal_iron", verts=12)
    blk("hook", (0.08, 0.08, 0.3), (0, 0, 2.06), "pal_iron_lt", bev=0)
    for k in range(10):
        a = k / 10 * math.tau
        cyl("bar_%d" % k, 0.035, 1.52, (math.cos(a) * 0.52, math.sin(a) * 0.52, 0.82), "pal_iron", verts=6)
    sphere("skull", 0.14, (0.1, -0.05, 0.62), "pal_bone")
    blk("ribs", (0.26, 0.16, 0.30), (0.02, 0.1, 0.36), "pal_bone")
    blk("bone", (0.06, 0.46, 0.06), (-0.15, -0.1, 0.12), "pal_bone", rot=(0, 0, 0.6))
    return 2.4


# =================================================================================
#  The throne room
# =================================================================================
def prop_demon_throne():
    """The throne: black iron wrought into a back of blades taller than two
    men, a pair of horns off its top that curl like the Cinder King's own,
    skulls under the arms, a crimson seat, all of it on three steps of stone
    with lava showing between them."""
    for k in range(3):
        w = 4.2 - k * 0.7
        blk("step_%d" % k, (w, 2.6 - k * 0.5, 0.26), (0, 0.2 + k * 0.18, 0.13 + k * 0.26), "pal_stone_dk" if k % 2 else "pal_stone",
            bev=0.02)
        blk("step_glow_%d" % k, (w + 0.04, 0.05, 0.05), (0, 0.2 + k * 0.18 - (2.6 - k * 0.5) / 2 - 0.01, 0.02 + k * 0.26),
            "pal_lava", emit=1.4, bev=0)
    z0 = 0.78
    blk("seat", (1.6, 1.1, 0.5), (0, 0.5, z0 + 0.25), "pal_iron", bev=0.02)
    blk("cushion", (1.3, 0.95, 0.16), (0, 0.45, z0 + 0.58), "pal_crimson", bev=0.03)
    blk("back", (1.7, 0.3, 3.2), (0, 1.05, z0 + 1.6), "pal_iron", bev=0.02)
    blk("back_cloth", (1.2, 0.08, 2.4), (0, 0.88, z0 + 1.5), "pal_crimson", bev=0)
    sphere("back_gem", 0.14, (0, 0.83, z0 + 2.5), "pal_glow", emit=1.4)
    for k in range(5):
        x = -0.72 + k * 0.36
        spike("blade_%d" % k, x, 1.05, z0 + 3.2, 0.14, 0.9 + (0.6 if k == 2 else 0.25 if k in (1, 3) else 0.0),
              "pal_iron_lt")
    for s in (-1, 1):
        blk("arm_%d" % s, (0.34, 1.1, 0.30), (s * 0.95, 0.45, z0 + 0.9), "pal_iron", bev=0.02)
        sphere("arm_skull_%d" % s, 0.2, (s * 0.95, -0.18, z0 + 0.9), "pal_bone")
        sphere("arm_eye_%d" % s, 0.05, (s * 0.95, -0.36, z0 + 0.94), "pal_glow", emit=1.4)
        blk("arm_post_%d" % s, (0.26, 0.26, 0.9), (s * 0.95, -0.05, z0 + 0.45), "pal_iron_lt")
        horn("throne_horn_%d" % s, s * 0.8, 1.05, z0 + 2.9, s, length=2.2, r=0.24)
        blk("wing_%d" % s, (0.9, 0.12, 2.2), (s * 1.2, 1.1, z0 + 1.6), "pal_iron", rot=(0, s * -0.35, 0), bev=0.02)
        for k in range(3):
            spike("wing_spike_%d_%d" % (s, k), s * (1.35 + k * 0.18), 1.1, z0 + 2.2 - k * 0.6, 0.08, 0.5, "pal_iron_lt",
                  tilt=(0, s * 0.9))
    return 6.2


def prop_totem_cinder_king():
    """The Cinder King's totem: the pit lord's black post, banded in molten
    gold, with his horns on top and his burning crown between them."""
    r = bp._totem("tt_cinder", "tt_cinder_b", "great_horns", eye_emit=1.8)
    cyl("crown", 0.15, 0.08, (0, 0, 1.13), "pal_gold", verts=12, metal=0.4)
    for k in range(5):
        a = k / 5 * math.tau
        cone("crown_pt_%d" % k, 0.035, 0.12, (math.cos(a) * 0.13, math.sin(a) * 0.13, 1.22), "pal_gold", verts=6)
    return r


bp.PALETTE.update({"tt_cinder": (0.120, 0.080, 0.090), "tt_cinder_b": (1.000, 0.620, 0.180)})


# =================================================================================
#  Registration: name -> (builder, final pixels across)
# =================================================================================
PROPS = {
    "palace_keep":  (prop_palace_keep, 576),
    "palace_tower": (prop_palace_tower, 320),
    "palace_torch": (prop_palace_torch, 64),
    "drawbridge":   (prop_drawbridge, 128),
    "lava_bridge":  (prop_lava_bridge, 128),
    # The foyer.
    "throne_door":       (prop_throne_door, 224),
    "palace_walkway":    (prop_palace_walkway, 112),
    "palace_pillar":     (prop_palace_pillar, 144),
    "palace_brazier":    (prop_palace_brazier, 72),
    "palace_banner":     (prop_palace_banner, 88),
    "demon_statue":      (prop_demon_statue, 96),
    "palace_sigil":      (prop_palace_sigil, 96),
    "palace_baluster":   (prop_palace_baluster, 44),
    # The rooms off it.
    "palace_chandelier": (prop_palace_chandelier, 84),
    "pipe_organ":        (prop_pipe_organ, 208),
    "palace_mirror":     (prop_palace_mirror, 88),
    "palace_table":      (prop_palace_table, 192),
    "palace_hearth":     (prop_palace_hearth, 140),
    "palace_bed":        (prop_palace_bed, 120),
    "cell_bars":         (prop_cell_bars, 84),
    "cell_door":         (prop_cell_door, 84),
    "wall_shackles":     (prop_wall_shackles, 64),
    "torture_rack":      (prop_torture_rack, 96),
    "iron_cage":         (prop_iron_cage, 76),
    # The throne room, and its master's totem.
    "demon_throne":      (prop_demon_throne, 200),
    "totem_cinder_king": (prop_totem_cinder_king, 32),
}
