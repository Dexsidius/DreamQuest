# =============================================================================
#  blender_frostreach_props.py - the Frostreach, off the Ice Spire: a snowman,
#  an igloo, the trapper's cabin on its islet in the frozen lake, the old
#  barrows and their runestones, the Warlord's Howe with its open door and
#  its braziers of pale fire, what lies about the camps (pelt racks, a
#  woodpile, a sled gone over, a hole cut in the ice for fishing), what sits
#  in the Howe (the draugr's high seat, a stone coffin) and the Abominable
#  Snowman's totem.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only howe_hall,snowman
#
#  Built with blender_props.py's own tools and registered into its PROPS
#  table, the way blender_hexmire_props.py is: that file hands itself over.
#
#  One Blender unit is about one 32px cell. The north is white and blue on
#  purpose, so everything made by hand stands out of it warm: brown logs and
#  hides, an orange carrot, a red scarf, a lit window. What is old and dead
#  there -- the barrows, the Howe, the draugr -- is grey stone and bone, and
#  its fire and its runes burn the same pale ice-blue as the Spire.
# =============================================================================

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

import blender_props as bp

blk, cyl, cone, sphere = bp.blk, bp.cyl, bp.cone, bp.sphere
BUILDING = bp.BUILDING_ELEVATION

bp.PALETTE.update({
    "fr_snow":      (0.930, 0.955, 0.985),
    "fr_snow_sh":   (0.700, 0.770, 0.860),
    "fr_snow_dk":   (0.520, 0.600, 0.700),
    "fr_snowball":  (0.900, 0.930, 0.970),
    "fr_ice":       (0.620, 0.855, 0.945),
    "fr_ice_pale":  (0.860, 0.955, 1.000),
    "fr_ice_deep":  (0.345, 0.600, 0.800),
    "fr_lake":      (0.700, 0.840, 0.920),
    "fr_water":     (0.060, 0.140, 0.230),
    "fr_water_lt":  (0.120, 0.260, 0.380),
    "fr_coal":      (0.090, 0.090, 0.100),
    "fr_carrot":    (0.960, 0.480, 0.110),
    "fr_carrot_dk": (0.760, 0.330, 0.080),
    "fr_scarf":     (0.760, 0.140, 0.120),
    "fr_scarf_dk":  (0.500, 0.080, 0.070),
    "fr_cream":     (0.900, 0.860, 0.740),
    "fr_twig":      (0.340, 0.240, 0.160),
    "fr_wood":      (0.470, 0.340, 0.220),
    "fr_wood_dk":   (0.300, 0.210, 0.140),
    "fr_wood_lt":   (0.640, 0.480, 0.310),
    "fr_log":       (0.420, 0.320, 0.230),
    "fr_log_dk":    (0.290, 0.220, 0.160),
    "fr_log_lt":    (0.540, 0.420, 0.300),
    "fr_log_end":   (0.800, 0.680, 0.500),
    "fr_log_ring":  (0.600, 0.470, 0.320),
    "fr_shingle":   (0.300, 0.250, 0.220),
    "fr_iron":      (0.200, 0.210, 0.240),
    "fr_iron_lt":   (0.400, 0.420, 0.460),
    "fr_stone":     (0.470, 0.490, 0.530),
    "fr_stone_dk":  (0.310, 0.330, 0.370),
    "fr_stone_lt":  (0.620, 0.640, 0.680),
    "fr_effigy":    (0.760, 0.760, 0.740),
    "fr_rock":      (0.300, 0.300, 0.330),
    "fr_rock_dk":   (0.190, 0.190, 0.220),
    "fr_void":      (0.035, 0.040, 0.055),
    "fr_bone":      (0.880, 0.850, 0.770),
    "fr_bone_dk":   (0.650, 0.610, 0.530),
    "fr_antler":    (0.820, 0.740, 0.600),
    "fr_hide":      (0.600, 0.420, 0.260),
    "fr_hide_dk":   (0.420, 0.280, 0.170),
    "fr_hide_lt":   (0.820, 0.690, 0.510),
    "fr_hide_mid":  (0.700, 0.530, 0.350),
    "fr_fur_grey":  (0.560, 0.560, 0.580),
    "fr_fur_grey_dk": (0.380, 0.380, 0.400),
    "fr_fur_grey_lt": (0.700, 0.700, 0.720),
    "fr_fur_grey_mid": (0.640, 0.640, 0.660),
    "fr_fur_white": (0.900, 0.890, 0.860),
    "fr_fur_red":   (0.720, 0.400, 0.180),
    "fr_fur_brown": (0.380, 0.260, 0.170),
    "fr_rope":      (0.640, 0.540, 0.360),
    "fr_lit":       (1.000, 0.780, 0.420),
    "fr_rune":      (0.500, 0.880, 1.000),
    "fr_flame":     (0.260, 0.560, 0.960),
    "fr_flame_mid": (0.500, 0.800, 1.000),
    "fr_flame_core": (0.820, 0.950, 1.000),
    "fr_grass":     (0.560, 0.500, 0.340),
    "fr_earth":     (0.330, 0.290, 0.240),
    "fr_yeti_face": (0.200, 0.230, 0.300),
    "tt_yeti":      (0.780, 0.770, 0.740),
    "tt_yeti_b":    (0.500, 0.850, 1.000),
})


# --- pieces -------------------------------------------------------------------
def frustum(name, r1, r2, h, loc, colour, rot=(0, 0, 0), verts=12, emit=0.0):
    """A cone cut off: a trunk that tapers, a bucket, a brazier's bowl."""
    bpy.ops.mesh.primitive_cone_add(radius1=r1, radius2=r2, depth=h, location=loc, vertices=verts)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = rot
    ob.data.materials.append(bp.material(name, colour, 0.8, 0.0, emit))
    return ob


def _aim(p0, p1):
    """The rotation that points an object's +Z from p0 toward p1, and the length."""
    dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    yaw = math.atan2(dx, -dy) if (dx or dy) else 0.0
    tilt = math.acos(max(-1.0, min(1.0, dz / length))) if length else 0.0
    return (tilt, 0.0, yaw), length


def rod(name, p0, p1, r, colour, verts=8, emit=0.0):
    """A round length from one point to another: a branch, a pole, a tine."""
    rot, length = _aim(p0, p1)
    mid = ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2, (p0[2] + p1[2]) / 2)
    ob = cyl(name, r, length, mid, colour, verts=verts, emit=emit)
    ob.rotation_euler = rot
    return ob


def spike(name, base, tip, r, colour, verts=8, emit=0.0):
    """A cone from its base toward its point: a carrot, an icicle, a tine."""
    rot, length = _aim(base, tip)
    mid = ((base[0] + tip[0]) / 2, (base[1] + tip[1]) / 2, (base[2] + tip[2]) / 2)
    return cone(name, r, length, mid, colour, rot=rot, verts=verts, emit=emit)


def blob(name, r, loc, colour, scale=(1, 1, 1), emit=0.0):
    ob = sphere(name, r, loc, colour, emit=emit)
    ob.scale = scale
    return ob


def clip_below(ob, z0=0.0):
    """Cuts away everything of a mesh under the ground. A dome or a drift made
    from a sphere sat on Z=0 otherwise shows its lower half under the front
    edge, and reads as a ball rather than as something sat on the snow."""
    bpy.context.view_layer.update()
    mw = ob.matrix_world
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    doomed = [v for v in bm.verts if (mw @ v.co).z < z0 - 1e-4]
    if doomed:
        bmesh.ops.delete(bm, geom=doomed, context="VERTS")
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def mound(name, r, loc, colour, scale=(1, 1, 1)):
    """A drift, a dome or a barrow: a sphere squashed to size, standing on the
    ground with nothing of it under the ground."""
    return clip_below(blob(name, r, loc, colour, scale))


def on_mound(x, y, cy, rx, ry, h):
    """The height of a mound's surface over (x, y): for setting a stone into
    it so that it shows, rather than guessing and burying it."""
    return h * math.sqrt(max(0.0, 1.0 - (x / rx) ** 2 - ((y - cy) / ry) ** 2))


def earth_skirt(prefix, cy, rx, ry, rng, n=10, grass=12):
    """Bare earth showing in a band round the foot of a snowed-over mound,
    the snow's edge ragged over it, and dead grass standing in it: what says
    "a mound of earth under snow" rather than "a heap of snow"."""
    mound(prefix + "_earth", 1.0, (0, cy, 0), "fr_earth", (rx + 0.14, ry + 0.1, 0.42))
    for k in range(n):
        a = math.pi + (k + 0.5) / n * math.pi + rng.uniform(-0.12, 0.12)
        mound("%s_tongue_%d" % (prefix, k), 0.3, (math.cos(a) * (rx - 0.05), cy + math.sin(a) * (ry - 0.05), 0),
              "fr_snow", (rng.uniform(1.1, 1.8), 1.0, rng.uniform(0.8, 1.1)))
    for k in range(grass):
        a = math.pi + (k + 0.5) / grass * math.pi + rng.uniform(-0.1, 0.1)
        x, y = math.cos(a) * (rx + 0.06), cy + math.sin(a) * (ry + 0.04)
        cone("%s_grass_%d" % (prefix, k), 0.05 + 0.02 * (k % 2), 0.3 + 0.1 * (k % 3), (x, y, 0.14), "fr_grass",
             verts=5, rot=(rng.uniform(-0.25, 0.25), rng.uniform(-0.3, 0.3), 0))


def facing(ob, t, n):
    """Turns a block so its X runs along `t` and its Y along the outward
    normal `n`: a snow block laid on a dome, a stone in an arch."""
    t = Vector(t).normalized()
    n = Vector(n).normalized()
    u = t.cross(n)
    m = Matrix((t, n, u)).transposed()
    ob.rotation_euler = m.to_euler()
    return ob


def group(before, rot=(0, 0, 0), loc=(0, 0, 0)):
    """Parents everything built since `before` (a set of objects) to an empty,
    and turns and moves that: a sled modelled upright, then tipped over."""
    pivot = bpy.data.objects.new("group", None)
    bpy.context.collection.objects.link(pivot)
    for ob in list(bpy.context.scene.objects):
        if ob in before or ob is pivot or ob.parent is not None:
            continue
        ob.parent = pivot
    pivot.rotation_euler = rot
    pivot.location = loc
    return pivot


def skull(name, x, y, z, s=1.0, eyes="fr_void", emit=0.0, colour="fr_bone"):
    """A skull facing the camera: crown, a narrower jaw, two sockets."""
    blob(name, 0.16 * s, (x, y, z), colour, (1.0, 0.95, 1.0))
    blk(name + "_jaw", (0.18 * s, 0.14 * s, 0.09 * s), (x, y - 0.03 * s, z - 0.13 * s), "fr_bone_dk", bev=0.01)
    for sx in (-1, 1):
        blk("%s_eye%d" % (name, sx), (0.065 * s, 0.03 * s, 0.065 * s), (x + sx * 0.062 * s, y - 0.15 * s, z),
            eyes, emit=emit, bev=0)


def torus(name, major, minor, loc, colour, rot=(0, 0, 0), scale=(1, 1, 1), emit=0.0):
    """A ring: a hoop, a brazier's rim, the lip of a hole in the ice."""
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, location=loc,
                                     major_segments=24, minor_segments=8)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = rot
    ob.scale = scale
    ob.data.materials.append(bp.material(name, colour, 0.8, 0.0, emit))
    return ob


def disc(name, r, depth, loc, colour, rot=(0, 0, 0), verts=20, emit=0.0):
    """A cylinder whose ends are fans of triangles, so clip_below can halve it
    and leave half a disc -- an arched doorway -- rather than a bare rim."""
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=depth, location=loc, vertices=verts, end_fill_type="TRIFAN")
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = rot
    ob.data.materials.append(bp.material(name, colour, 0.8, 0.0, emit))
    return ob


def antlers(name, x, y, z, s=1.0, colour="fr_antler", wide=1.0, tall=1.0):
    """A great rack of antlers, facing the camera: a beam each side sweeping
    out and up, tines off the top of it, and a brow tine forward. `wide` and
    `tall` stretch it, for a rack laid across the back of a seat."""
    for sx in (-1, 1):
        p0 = (x + sx * 0.12 * s * wide, y, z)
        p1 = (x + sx * 0.55 * s * wide, y + 0.05 * s, z + 0.30 * s * tall)
        p2 = (x + sx * 0.85 * s * wide, y + 0.08 * s, z + 0.75 * s * tall)
        p3 = (x + sx * 0.92 * s * wide, y + 0.1 * s, z + 1.15 * s * tall)
        for k, (a, b) in enumerate(((p0, p1), (p1, p2), (p2, p3))):
            rod("%s_beam_%d_%d" % (name, sx, k), a, b, (0.075 - 0.012 * k) * s, colour, verts=8)
            blob("%s_knee_%d_%d" % (name, sx, k), (0.075 - 0.012 * k) * s, b, colour)
        for k, (t, h, lean) in enumerate(((0.35, 0.40, 0.05), (0.65, 0.42, 0.12), (0.92, 0.34, 0.25))):
            bx = p1[0] + (p3[0] - p1[0]) * t
            bz = p1[2] + (p3[2] - p1[2]) * t
            spike("%s_tine_%d_%d" % (name, sx, k), (bx, y + 0.08 * s, bz),
                  (bx - sx * lean * s, y + 0.06 * s, bz + h * s * tall), 0.05 * s, colour, verts=6)
        spike("%s_brow_%d" % (name, sx), (x + sx * 0.3 * s * wide, y, z + 0.14 * s * tall),
              (x + sx * 0.42 * s * wide, y - 0.32 * s, z + 0.30 * s * tall), 0.05 * s, colour, verts=6)


def elk_skull(name, x, y, z, s=1.0):
    """The long skull the antlers grow from, nose down, facing the camera."""
    blob(name, 0.2 * s, (x, y, z), "fr_bone", (1.0, 0.9, 0.9))
    blk(name + "_snout", (0.2 * s, 0.14 * s, 0.34 * s), (x, y - 0.08 * s, z - 0.25 * s), "fr_bone", bev=0.03 * s)
    blk(name + "_nose", (0.14 * s, 0.1 * s, 0.1 * s), (x, y - 0.14 * s, z - 0.4 * s), "fr_bone_dk", bev=0.02 * s)
    for sx in (-1, 1):
        blk("%s_eye%d" % (name, sx), (0.08 * s, 0.05 * s, 0.08 * s), (x + sx * 0.1 * s, y - 0.16 * s, z - 0.02 * s),
            "fr_void", bev=0)


def icicles(name, x0, x1, y, z, n, length=0.28, r=0.05, seed=1):
    """A row of icicles hanging from an edge, of uneven lengths."""
    for k in range(n):
        t = (k + 0.5) / n
        h = ((k * 37 + seed * 11) % 7) / 7.0
        ln = length * (0.45 + 0.75 * h)
        x = x0 + (x1 - x0) * t
        cone("%s_%d" % (name, k), r * (0.8 + 0.4 * h), ln, (x, y, z - ln / 2), "fr_ice_pale" if k % 2 else "fr_ice",
             rot=(math.pi, 0, 0), verts=6)


def snow_cap(name, w, d, loc, h=0.14, colour="fr_snow"):
    """Snow lying on top of something: a flattened lump, rounded at the edges."""
    return blob(name, 0.5, loc, colour, (w, d, h * 2))


GLYPHS = {
    "tiwaz":  ((0, -0.12, 0, 0.12), (0, 0.12, -0.1, 0.02), (0, 0.12, 0.1, 0.02)),
    "algiz":  ((0, -0.12, 0, 0.12), (0, 0.0, -0.1, 0.11), (0, 0.0, 0.1, 0.11)),
    "fehu":   ((-0.05, -0.12, -0.05, 0.12), (-0.05, 0.0, 0.07, 0.11), (-0.05, -0.08, 0.07, 0.03)),
    "sowilo": ((-0.07, 0.12, 0.06, 0.04), (0.06, 0.04, -0.06, -0.04), (-0.06, -0.04, 0.07, -0.12)),
}


def rune_glyph(name, x, y, z, kind, s=1.0, emit=1.3, colour="fr_rune"):
    """One rune on a flat face at depth y, its strokes from GLYPHS: lit, or
    with emit=0 and a dark colour, only cut."""
    for k, (x0, z0, x1, z1) in enumerate(GLYPHS[kind]):
        rod("%s_%d" % (name, k), (x + x0 * s, y, z + z0 * s), (x + x1 * s, y, z + z1 * s), 0.032 * s, colour,
            verts=4, emit=emit)


def ice_shard(name, h, r, loc, lean, colour):
    bpy.ops.mesh.primitive_cone_add(radius1=r, radius2=0.0, depth=h, location=(loc[0], loc[1], loc[2] + h / 2),
                                    vertices=6)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = lean
    ob.data.materials.append(bp.material(name, colour, 0.3, 0.0, 0.1))
    for p in ob.data.polygons:
        p.use_smooth = False
    return ob


# =================================================================================
#  Out on the snow
# =================================================================================

def prop_snowman():
    """A snowman: three balls of snow, coal for his eyes and his buttons, a
    carrot for a nose turned a little aside so it shows as a carrot, stick
    arms with twig fingers, a red scarf with a tail blown out, and an old
    bucket on his head for a hat. Stood in a drift of his own snow."""
    # The drift is a shade bluer than he is, and low, so his bottom ball
    # still reads as a ball sat in it rather than as the drift heaped up.
    mound("drift", 0.5, (0, 0.04, 0), "fr_snow_sh", (1.1, 0.85, 0.14))
    mound("drift_b", 0.14, (0.4, -0.22, 0), "fr_snow", (1.0, 0.9, 0.5))
    mound("drift_c", 0.11, (-0.42, -0.16, 0), "fr_snow", (1.0, 0.9, 0.6))
    clip_below(blob("base", 0.33, (0, 0, 0.3), "fr_snowball", (1.0, 1.0, 0.95)))
    sphere("button_3", 0.042, (0, -0.31, 0.38), "fr_coal")
    # Everything above his bottom ball is built where it would sit, then
    # lifted clear of it, so there is a waist between the balls to see.
    before = set(bpy.context.scene.objects)
    blob("body", 0.25, (0, 0, 0.74), "fr_snowball", (1.0, 1.0, 0.95))
    blob("head", 0.2, (0, 0, 1.08), "fr_snowball")
    # The face.
    for sx in (-1, 1):
        sphere("eye_%d" % sx, 0.042, (sx * 0.075, -0.165, 1.13), "fr_coal")
    spike("nose", (0.0, -0.17, 1.07), (0.12, -0.44, 1.03), 0.05, "fr_carrot", verts=8)
    for k, a in enumerate((-0.5, -0.17, 0.17, 0.5)):
        sphere("mouth_%d" % k, 0.024, (math.sin(a) * 0.1, -0.18, 0.99 - math.cos(a) * 0.035 + 0.035), "fr_coal")
    # Buttons down his front.
    for k, z in enumerate((0.84, 0.72, 0.60)):
        sphere("button_%d" % k, 0.04, (0, -0.24 - (0.02 if k == 1 else 0.0), z), "fr_coal")
    # The scarf: a band round the neck, and a tail hung down his front and
    # blown a little aside, striped at the end.
    cyl("scarf", 0.2, 0.1, (0, 0, 0.92), "fr_scarf", verts=16)
    cyl("scarf_roll", 0.215, 0.05, (0, 0, 0.95), "fr_scarf_dk", verts=16)
    blk("scarf_tail", (0.11, 0.05, 0.34), (0.12, -0.22, 0.78), "fr_scarf", rot=(0.25, -0.35, 0), bev=0.01)
    blk("scarf_band", (0.12, 0.055, 0.05), (0.165, -0.25, 0.68), "fr_cream", rot=(0.25, -0.35, 0), bev=0)
    for k in range(3):
        blk("fringe_%d" % k, (0.025, 0.04, 0.07), (0.17 + (k - 1) * 0.035, -0.27, 0.6), "fr_scarf_dk",
            rot=(0.25, -0.35, 0), bev=0)
    # Stick arms, one raised as if to wave.
    for sx, lift in ((-1, 0.18), (1, 0.34)):
        p0 = (sx * 0.2, 0.0, 0.78)
        p1 = (sx * 0.62, -0.04, 0.8 + lift)
        rod("arm_%d" % sx, p0, p1, 0.035, "fr_twig", verts=6)
        rod("finger_a_%d" % sx, p1, (p1[0] + sx * 0.14, p1[1], p1[2] + 0.12), 0.024, "fr_twig", verts=5)
        rod("finger_b_%d" % sx, p1, (p1[0] + sx * 0.17, p1[1], p1[2] - 0.04), 0.024, "fr_twig", verts=5)
        mid = (p0[0] + (p1[0] - p0[0]) * 0.6, p0[1], p0[2] + (p1[2] - p0[2]) * 0.6)
        rod("twig_%d" % sx, mid, (mid[0] + sx * 0.02, mid[1], mid[2] + 0.14), 0.02, "fr_twig", verts=5)
    # An old bucket for a hat, tipped a little, with iron hoops on it.
    tip = (0, -0.12, 0.15)
    frustum("bucket", 0.18, 0.14, 0.24, (0.01, 0.0, 1.35), "fr_wood", rot=tip, verts=12)
    for k, (z, r) in enumerate(((1.27, 0.185), (1.41, 0.155))):
        cyl("hoop_%d" % k, r, 0.035, (0.01 + (z - 1.35) * 0.15, (z - 1.35) * 0.12, z), "fr_iron", rot=tip, verts=12)
    cyl("bucket_top", 0.13, 0.02, (0.05, 0.03, 1.47), "fr_wood_dk", rot=tip, verts=12)
    rod("bail", (-0.17, -0.02, 1.3), (-0.1, -0.14, 1.12), 0.015, "fr_iron", verts=5)
    group(before, loc=(0, 0, 0.07))
    return 1.95


def prop_igloo():
    """An igloo: a dome laid in courses of snow blocks, the joints between
    them a shade of blue so every block shows, a key block on the crown, and
    a tunnel of blocks coming out toward the camera to a dark, low doorway.
    Snow banked up round its foot. Pushed back from the frame's middle so the
    tunnel and the drifts in front of it are not cut off at the bottom."""
    R, cy = 1.3, 0.9
    gap = 0.07
    mound("dome", R, (0, cy, 0), "fr_snow_sh")
    courses, top_el = 5, math.radians(78)
    for c in range(courses):
        el0, el1 = top_el * c / courses, top_el * (c + 1) / courses
        el = (el0 + el1) / 2
        rr = R * math.cos(el)
        n = max(5, int(round(math.tau * rr / 0.62)))
        w, h = math.tau * rr / n - gap, R * (el1 - el0) - gap
        for k in range(n):
            az = (k + 0.5 * (c % 2)) / n * math.tau + 0.2
            nrm = (math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el))
            if nrm[1] > 0.75 and c < 3:
                continue        # the far side: never seen
            loc = (nrm[0] * (R + 0.02), cy + nrm[1] * (R + 0.02), nrm[2] * (R + 0.02))
            ob = blk("block_%d_%d" % (c, k), (w, 0.14, h), loc, "fr_snow" if (c + k) % 3 else "fr_snowball",
                     bev=0.025)
            facing(ob, (-math.sin(az), math.cos(az), 0), nrm)
    cyl("crown", R * math.cos(top_el) + 0.03, 0.14, (0, cy, R * math.sin(math.radians(84))), "fr_snow", verts=12)
    # The tunnel: a half-round of blocks in two rings, on a darker body, and
    # the doorway a dark half-disc in the front of it.
    RT, y_f = 0.66, cy - R - 0.75
    y_b = cy - R + 0.35
    clip_below(disc("tunnel", RT, y_b - y_f, (0, (y_f + y_b) / 2, 0), "fr_snow_sh", rot=(math.pi / 2, 0, 0)))
    ring = (y_b - y_f - 0.3) / 2
    for j, n in enumerate((5, 4)):
        yc = y_f + ring / 2 + j * ring
        for k in range(n):
            th = (k + 0.5) / n * math.pi
            w = math.pi * RT / n - gap
            nrm = (math.cos(th), 0, math.sin(th))
            ob = blk("tblock_%d_%d" % (j, k), (w, 0.14, ring - gap), (nrm[0] * (RT + 0.02), yc, nrm[2] * (RT + 0.02)),
                     "fr_snow" if (j + k) % 2 else "fr_snowball", bev=0.025)
            facing(ob, (-math.sin(th), 0, math.cos(th)), nrm)
    clip_below(disc("door", 0.47, 0.06, (0, y_f - 0.03, 0), "fr_void", rot=(math.pi / 2, 0, 0)))
    # Snow round the foot: one smooth apron, banks heaped against the dome,
    # and a bank either side of the doorway where it was dug out.
    mound("apron", 1.0, (0, cy + 0.05, 0), "fr_snow", (R + 0.38, R + 0.3, 0.22))
    for k, az in enumerate((-2.3, -0.84, 2.8, 0.34)):
        mound("bank_%d" % k, 0.42, (math.cos(az) * (R - 0.12), cy + math.sin(az) * (R - 0.12), 0),
              "fr_snowball", (1.7, 1.05, 0.55))
    for s in (-1, 1):
        mound("door_bank_%d" % s, 0.3, (s * (RT + 0.14), y_f + 0.12, 0), "fr_snow", (1.1, 1.0, 0.75))
    return 4.0


def prop_pelt_rack():
    """A drying frame of poles: two posts and a middle one, rails top and
    bottom, a brown deer hide laced into one bay and a grey wolf's into the
    other, and a fox skin, tail and all, hung over the top rail."""
    for k, x in enumerate((-0.9, 0.0, 0.9)):
        cyl("post_%d" % k, 0.06 if k != 1 else 0.045, 1.5, (x, 0.05, 0.75), "fr_log_dk", rot=(-0.07, 0, 0), verts=8)
        mound("post_snow_%d" % k, 0.16, (x, 0.02, 0), "fr_snow", (1.3, 1.0, 0.6))
    for z, y in ((0.18, 0.012), (1.42, 0.1)):
        cyl("rail_%.2f" % z, 0.05, 1.98, (0, y, z), "fr_log", rot=(0, math.pi / 2, 0), verts=8)
    for sx in (-1, 1):
        cyl("brace_%d" % sx, 0.035, 1.3, (sx * 0.9, 0.5, 0.58), "fr_log_dk", rot=(0.62, 0, 0), verts=6)
    # A skin laced into each bay: a sheet with a darker rim, flaps at the
    # corners where the legs were, and thongs from its edge out to the poles
    # and the rails all the way round -- the thongs are what say "stretched".
    for name, cx, colour, rim, ears in (("deer", -0.45, "fr_hide", "fr_hide_dk", False),
                                        ("wolf", 0.45, "fr_fur_grey", "fr_fur_grey_dk", True)):
        w, h, cz = 0.54, 0.8, 0.8
        blk(name + "_rim", (w + 0.07, 0.03, h + 0.07), (cx, 0.012, cz), rim, bev=0.01)
        blk(name, (w, 0.035, h), (cx, 0.004, cz), colour, bev=0.01)
        blob(name + "_mid", 0.5, (cx, 0.0, cz), "fr_hide_mid" if name == "deer" else "fr_fur_grey_mid",
             (w * 0.62, 0.04, h * 0.78))
        for sx in (-1, 1):
            for sz in (-1, 1):
                blk("%s_leg_%d_%d" % (name, sx, sz), (0.09, 0.03, 0.2),
                    (cx + sx * (w / 2 + 0.03), 0.01, cz + sz * (h / 2 + 0.02)), rim, rot=(0, sx * sz * -0.8, 0),
                    bev=0.01)
            for k in range(3):
                blk("%s_lace_%d_%d" % (name, sx, k), (0.14, 0.02, 0.025), (cx + sx * 0.36, -0.01, cz - 0.24 + k * 0.24),
                    "fr_rope", bev=0)
        for sz in (-1, 1):
            for k in (-1, 1):
                blk("%s_vlace_%d_%d" % (name, sz, k), (0.025, 0.02, 0.16), (cx + k * 0.14, -0.01, cz + sz * 0.5),
                    "fr_rope", bev=0)
        blk(name + "_neck", (w * 0.32, 0.03, 0.12), (cx, 0.01, cz + h / 2 + 0.07), rim, bev=0.01)
        if ears:
            for sx in (-1, 1):
                spike("%s_ear_%d" % (name, sx), (cx + sx * 0.07, 0.0, cz + h / 2 + 0.1),
                      (cx + sx * 0.11, 0.0, cz + h / 2 + 0.24), 0.05, rim, verts=4)
    # The fox, over the top rail: body down the front, tail with a white tip.
    blk("fox", (0.24, 0.05, 0.42), (0.62, -0.02, 1.24), "fr_fur_red", bev=0.02)
    blk("fox_back", (0.24, 0.05, 0.2), (0.62, 0.16, 1.35), "fr_fur_red", bev=0.02)
    cyl("fox_over", 0.075, 0.24, (0.62, 0.08, 1.46), "fr_fur_red", rot=(0, math.pi / 2, 0), verts=8)
    spike("fox_tail", (0.68, -0.04, 1.08), (0.78, -0.08, 0.66), 0.08, "fr_fur_red", verts=8)
    blob("fox_tip", 0.05, (0.78, -0.08, 0.68), "fr_fur_white")
    spike("fox_ear", (0.62, -0.03, 1.02), (0.62, -0.05, 0.92), 0.06, "fr_fur_red", verts=5)
    return 2.25


def prop_woodpile():
    """Split logs stacked end-on to the camera between two stakes, snow lying
    on the top of the stack, and beside it a chopping block with an axe
    stood in it and chips round its foot."""
    import random
    rng = random.Random(63)
    rows = (5, 5, 4)
    x0 = -0.28
    top = 0.0
    for r, count in enumerate(rows):
        for c in range(count):
            x = x0 + (c - (count - 1) / 2) * 0.23
            z = 0.12 + r * 0.2
            top = max(top, z + 0.11)
            verts = 5 if (r + c) % 2 else 6
            spin = rng.uniform(0, math.pi)
            cyl("log_%d_%d" % (r, c), 0.115, 0.78, (x, 0, z), ("fr_log", "fr_log_dk", "fr_log_lt")[(r + c) % 3],
                rot=(math.pi / 2, spin, 0), verts=verts)
            cyl("end_%d_%d" % (r, c), 0.098, 0.02, (x, -0.395, z), "fr_log_end", rot=(math.pi / 2, spin, 0),
                verts=verts)
            blk("split_%d_%d" % (r, c), (0.03, 0.022, 0.1), (x + 0.02, -0.41, z + 0.01), "fr_log_ring",
                rot=(0, spin, 0), bev=0)
    for sx in (-1, 1):
        cyl("stake_%d" % sx, 0.04, 0.95, (x0 + sx * 0.62, -0.2, 0.47), "fr_log_dk", verts=6)
        blob("stake_cap_%d" % sx, 0.06, (x0 + sx * 0.62, -0.2, 0.97), "fr_snow", (1.2, 1.2, 0.7))
    # Snow on the top of the stack, lumpy, and some caught on the ends.
    for k, (dx, dy, w, d) in enumerate(((-0.3, 0.05, 0.5, 0.62), (0.2, 0.12, 0.56, 0.5), (0.02, -0.22, 0.4, 0.3),
                                        (0.34, -0.18, 0.22, 0.24))):
        snow_cap("snow_top_%d" % k, w * 1.1, d * 1.05, (x0 + dx, dy, top - 0.03), h=0.045)
    for k, (dx, dz) in enumerate(((-0.46, 0.34), (0.3, 0.14), (-0.12, 0.54))):
        snow_cap("snow_ledge_%d" % k, 0.2, 0.12, (x0 + dx, -0.38, dz + 0.1), h=0.06)
    mound("drift", 0.5, (x0, 0.05, 0), "fr_snow", (1.25, 1.0, 0.18))
    # The chopping block and the axe stood in it.
    bx, by = 0.62, -0.12
    cyl("block", 0.25, 0.42, (bx, by, 0.21), "fr_log", verts=12)
    cyl("block_top", 0.23, 0.02, (bx, by, 0.425), "fr_log_end", verts=12)
    cyl("block_ring", 0.13, 0.024, (bx, by, 0.43), "fr_log_ring", verts=10)
    blk("axe_head", (0.26, 0.07, 0.16), (bx - 0.02, by - 0.02, 0.47), "fr_iron_lt", rot=(0, 0.35, 0), metal=0.5)
    rod("axe_haft", (bx + 0.06, by - 0.02, 0.5), (bx + 0.24, by - 0.1, 1.0), 0.04, "fr_wood_lt", verts=8)
    for k in range(5):
        a = k * 1.3 + 0.4
        blk("chip_%d" % k, (0.1, 0.05, 0.03), (bx + math.cos(a) * 0.28, by + math.sin(a) * 0.3 - 0.05, 0.015),
            "fr_log_end", rot=(0, 0, a), bev=0)
    return 2.0


def curl(name, x, y, z0, r, colour, a0=-90, a1=110, step=25, thick=0.04):
    """The upswept nose of a runner: a run of short rods round an arc."""
    pts = []
    a = a0
    while a <= a1:
        t = math.radians(a)
        pts.append((x + r * math.cos(t), y, z0 + r + r * math.sin(t)))
        a += step
    for k, (p, q) in enumerate(zip(pts, pts[1:])):
        rod("%s_%d" % (name, k), p, q, thick, colour, verts=6)
        blob("%s_j_%d" % (name, k), thick, q, colour)


def _sled(snapped, handle=True):
    """A freight sled standing upright, its nose toward +X: two runners with
    their noses curled up, stanchions, a deck of planks with a rail each side,
    and a handlebar at the back. The runner on the `snapped` side (-1 or 1)
    is broken off short, splintered, and so is its rail."""
    L0, L1 = -0.8, 0.55
    for sy in (-1, 1):
        yy = sy * 0.3
        if sy != snapped:
            blk("runner_%d" % sy, (L1 - L0, 0.08, 0.08), ((L0 + L1) / 2, yy, 0.04), "fr_wood_dk", bev=0.01)
            curl("nose_%d" % sy, L1, yy, 0.0, 0.26, "fr_wood_dk", thick=0.045)
        else:
            blk("runner_%d" % sy, (0.8, 0.08, 0.08), (L0 + 0.4, yy, 0.04), "fr_wood_dk", bev=0.01)
            spike("snap_%d" % sy, (L0 + 0.78, yy, 0.045), (L0 + 1.0, yy, 0.07), 0.05, "fr_wood_lt", verts=5)
            spike("snap2_%d" % sy, (L0 + 0.78, yy, 0.02), (L0 + 0.92, yy + 0.02, -0.02), 0.035, "fr_wood_lt", verts=5)
        for x in (-0.66, -0.1, 0.42):
            if sy == snapped and x > 0.0:
                continue
            blk("stanchion_%d_%.2f" % (sy, x), (0.07, 0.07, 0.44), (x, yy, 0.25), "fr_wood", bev=0.01)
    for x in (-0.66, -0.1, 0.42):
        blk("bar_%.2f" % x, (0.08, 0.66, 0.07), (x, 0, 0.3), "fr_wood_dk", bev=0.01)
    for j in range(4):
        blk("plank_%d" % j, (1.42, 0.13, 0.05), (-0.12, -0.225 + j * 0.15, 0.36), ("fr_wood_lt", "fr_wood")[j % 2],
            bev=0.01)
    for sy in (-1, 1):
        if sy == snapped:
            blk("rail_%d" % sy, (0.75, 0.06, 0.07), (-0.44, sy * 0.32, 0.47), "fr_wood_dk", bev=0.01)
        else:
            blk("rail_%d" % sy, (1.45, 0.06, 0.07), (-0.12, sy * 0.32, 0.47), "fr_wood_dk", bev=0.01)
        if handle:
            blk("handle_%d" % sy, (0.06, 0.06, 0.42), (L0 + 0.02, sy * 0.3, 0.64), "fr_wood", bev=0.01)
    if handle:
        blk("handle_bar", (0.07, 0.68, 0.07), (L0 + 0.02, 0, 0.84), "fr_wood_dk", bev=0.01)


def _spill(y0=0.0):
    """The load, thrown out: a roll of brown fur roped twice, a grey skin
    lying spread with its tail, a white one heaped, and the snapped-off nose
    of the runner lying on the snow."""
    blk("piece", (0.5, 0.08, 0.08), (0.42, y0 - 0.42, 0.04), "fr_wood_dk", rot=(0, 0, -0.35), bev=0.01)
    curl("piece_nose", 0.62, y0 - 0.49, 0.0, 0.2, "fr_wood_dk", a1=100, thick=0.045)
    spike("piece_snap", (0.2, y0 - 0.34, 0.04), (0.06, y0 - 0.3, 0.06), 0.045, "fr_wood_lt", verts=5)
    blob("roll", 0.2, (-0.42, y0 - 0.36, 0.13), "fr_fur_brown", (1.45, 0.85, 0.7))
    blob("roll_fold", 0.12, (-0.62, y0 - 0.44, 0.12), "fr_hide", (1.0, 0.8, 0.8))
    blk("roll_tie", (0.05, 0.36, 0.3), (-0.34, y0 - 0.36, 0.14), "fr_rope", bev=0.02)
    blob("grey_skin", 0.3, (0.05, y0 - 0.5, 0.02), "fr_fur_grey", (1.4, 0.7, 0.14))
    spike("grey_tail", (0.42, y0 - 0.52, 0.03), (0.66, y0 - 0.62, 0.03), 0.06, "fr_fur_grey_dk", verts=6)
    blob("white_skin", 0.22, (-0.66, y0 - 0.2, 0.09), "fr_fur_white", (1.3, 0.8, 0.3))


def prop_broken_sled():
    """A freight sled gone over: upside down, its runners in the air, the far
    one whole with its curled nose hooked down to the snow and the near one
    snapped short, its nose lying in front; the load spilled out of it."""
    before = set(bpy.context.scene.objects)
    _sled(1, handle=False)
    group(before, rot=(math.radians(180), 0, math.radians(8)), loc=(0.0, 0.3, 0.54))
    _spill(0.04)
    mound("drift", 0.45, (-0.2, 0.65, 0), "fr_snow", (1.8, 0.8, 0.28))
    mound("drift_b", 0.2, (0.8, 0.3, 0), "fr_snow", (1.3, 1.0, 0.5))
    return 2.25


def prop_ice_hole():
    """A hole cut in the lake ice for fishing: dark water in a round mouth,
    the cut edge of the ice showing blue inside the rim, broken chips of ice
    thrown round it, and a fishing stick propped on a fork of twig with its
    line going down into the water. Seen from high, as it lies flat."""
    cy = 0.2
    clip_below(torus("rim", 0.43, 0.14, (0, cy, 0), "fr_ice_deep", scale=(1.0, 1.0, 0.45)))
    import random
    rng = random.Random(66)
    for k in range(13):
        a = k / 13 * math.tau + rng.uniform(-0.1, 0.1)
        d = 0.44 + rng.uniform(-0.02, 0.05)
        ob = bp.rock("rim_%d" % k, (rng.uniform(0.09, 0.13), rng.uniform(0.07, 0.1), rng.uniform(0.04, 0.07)),
                     (math.cos(a) * d, cy + math.sin(a) * d, 0.04), "fr_ice_pale" if k % 3 else "fr_ice", rng)
        ob.rotation_euler = (0, 0, a + math.pi / 2)
    cyl("water", 0.31, 0.02, (0, cy, 0.0), "fr_water", verts=20)
    blk("glint", (0.12, 0.03, 0.01), (-0.08, cy + 0.08, 0.012), "fr_water_lt", bev=0)
    blk("glint2", (0.06, 0.03, 0.01), (0.1, cy - 0.1, 0.012), "fr_water_lt", bev=0)
    for k, (a, d, s) in enumerate(((0.3, 0.66, 0.1), (1.1, 0.7, 0.08), (2.0, 0.64, 0.11), (2.8, 0.7, 0.07),
                                   (3.6, 0.66, 0.1), (4.3, 0.72, 0.08), (5.1, 0.64, 0.09), (5.8, 0.72, 0.06))):
        blk("chip_%d" % k, (s * 1.3, s, s * 0.6), (math.cos(a) * d, cy + math.sin(a) * d * 0.9, s * 0.3),
            "fr_ice_pale" if k % 2 else "fr_ice", rot=(0.3 * (k % 3), 0.2, a), bev=0.01)
    for k, (a, r) in enumerate(((0.8, 0.14), (3.3, 0.12), (4.8, 0.1))):
        mound("scuff_%d" % k, r, (math.cos(a) * 0.5, cy + math.sin(a) * 0.55, 0), "fr_snow", (1.5, 1.0, 0.3))
    # The stick: its butt on the ice, resting in a fork, its tip over the water.
    rod("fork", (0.46, cy + 0.12, 0.0), (0.44, cy + 0.1, 0.2), 0.022, "fr_twig", verts=5)
    rod("fork_a", (0.44, cy + 0.1, 0.18), (0.4, cy + 0.08, 0.26), 0.018, "fr_twig", verts=5)
    rod("fork_b", (0.44, cy + 0.1, 0.18), (0.5, cy + 0.1, 0.26), 0.018, "fr_twig", verts=5)
    rod("stick", (0.72, cy + 0.26, 0.03), (0.02, cy - 0.06, 0.3), 0.03, "fr_wood", verts=6)
    rod("line", (0.02, cy - 0.06, 0.3), (0.0, cy - 0.08, 0.0), 0.016, "fr_cream", verts=4)
    blob("float", 0.03, (0.0, cy - 0.08, 0.02), "fr_scarf")
    return (1.5, 60.0)


def brazier(name, x, y, s=1.0):
    """An iron brazier on three legs, a bowl with a heavy rim, and a fire in
    it that burns pale blue-white. Rime hangs off the underside of the rim."""
    for k in range(3):
        a = math.pi / 2 + k * math.tau / 3
        foot = (x + math.cos(a) * 0.38 * s, y + math.sin(a) * 0.38 * s, 0.0)
        rod("%s_leg_%d" % (name, k), foot, (x + math.cos(a) * 0.2 * s, y + math.sin(a) * 0.2 * s, 0.8 * s),
            0.05 * s, "fr_iron", verts=6)
        blob("%s_foot_%d" % (name, k), 0.07 * s, (foot[0], foot[1], 0.03 * s), "fr_iron", (1.2, 1.2, 0.6))
    torus(name + "_stay", 0.27 * s, 0.03 * s, (x, y, 0.36 * s), "fr_iron")
    frustum(name + "_bowl", 0.18 * s, 0.42 * s, 0.3 * s, (x, y, 0.93 * s), "fr_iron", verts=14)
    torus(name + "_rim", 0.42 * s, 0.055 * s, (x, y, 1.08 * s), "fr_iron_lt")
    cyl(name + "_coals", 0.38 * s, 0.04 * s, (x, y, 1.06 * s), "fr_ice_deep", verts=14, emit=0.6)
    frost_fire(name + "_fire", x, y, 1.06 * s, 0.3 * s, 0.62 * s)
    for k, a in enumerate((-2.4, -1.9, -1.35, -0.8)):
        cone("%s_rime_%d" % (name, k), 0.035 * s, 0.14 * s, (x + math.cos(a) * 0.4 * s, y + math.sin(a) * 0.4 * s,
             1.0 * s), "fr_ice_pale", rot=(math.pi, 0, 0), verts=5)


def frost_fire(name, x, y, z, r, h, emit=1.2):
    """A flame of the north's own: a crown of blue tongues leaning out round
    a taller middle one, paler toward the heart, and a white core."""
    spike(name, (x, y, z), (x, y, z + h), r * 0.62, "fr_flame", verts=8, emit=emit)
    for k, (a, hh) in enumerate(((0.3, 0.62), (1.55, 0.78), (2.8, 0.58), (3.9, 0.7), (5.1, 0.6))):
        px, py = x + math.cos(a) * r * 0.55, y + math.sin(a) * r * 0.55
        spike("%s_tongue_%d" % (name, k), (px, py, z), (px + math.cos(a) * r * 0.35, py + math.sin(a) * r * 0.3,
              z + h * hh), r * 0.4, "fr_flame" if k % 2 else "fr_flame_mid", verts=7, emit=emit)
    spike(name + "_mid", (x, y - r * 0.2, z), (x + r * 0.05, y - r * 0.25, z + h * 0.8), r * 0.45, "fr_flame_mid",
          verts=7, emit=emit)
    spike(name + "_core", (x, y - r * 0.35, z), (x, y - r * 0.35, z + h * 0.5), r * 0.32, "fr_flame_core", verts=7,
          emit=1.5)


def prop_frost_brazier():
    """The north's lamp: an iron brazier on three legs with pale fire in it."""
    brazier("brazier", 0, 0, 1.0)
    mound("snow", 0.4, (0, 0.02, 0), "fr_snow", (1.3, 1.1, 0.16))
    return 2.0


def prop_runestone():
    """A tall standing stone with its top rounded off, a serpent band cut
    round the edge of its face and runes cut in a column down the middle,
    all of them glowing ice-blue; snow on its shoulders and drifted round
    its foot, and crystals of ice grown up out of the drift."""
    before = set(bpy.context.scene.objects)
    H, W, T = 1.78, 0.72, 0.34
    body_h = H - W / 2
    ob = cyl("stone", 1.0, body_h, (0, 0, body_h / 2), "fr_stone", verts=10)
    ob.scale = (W / 2, T / 2, 1.0)
    ob = sphere("top", 1.0, (0, 0, body_h), "fr_stone")
    ob.scale = (W / 2, T / 2, W / 2 * 1.05)
    ob.rotation_euler = (0, 0.2, 0)
    glow, e = "fr_rune", 1.4

    def face(x):
        """Where the curved face of the stone is, a hair proud of it."""
        return -T / 2 * math.sqrt(max(0.0, 1.0 - (x / (W / 2)) ** 2)) - 0.01

    # The serpent band: up one side, over the top, down the other.
    inset = 0.11
    for sx in (-1, 1):
        x = sx * (W / 2 - inset)
        blk("band_side_%d" % sx, (0.06, 0.06, body_h - 0.3), (x, face(x), 0.3 + (body_h - 0.3) / 2),
            glow, emit=e, bev=0)
    arc_r = W / 2 - inset
    for k in range(7):
        a0, a1 = math.pi * k / 7, math.pi * (k + 1) / 7
        x0, x1 = math.cos(a0) * arc_r, math.cos(a1) * arc_r
        p = (x0, face(x0) - 0.01, body_h + math.sin(a0) * arc_r * 0.9)
        q = (x1, face(x1) - 0.01, body_h + math.sin(a1) * arc_r * 0.9)
        rod("band_arc_%d" % k, p, q, 0.032, glow, verts=4, emit=e)
    x = -(W / 2 - inset)
    blob("band_head", 0.06, (x, face(x), 0.3), glow, emit=e)

    # Runes down the middle, following the curve of the face.
    for k, (z, kind) in enumerate(((1.22, "tiwaz"), (0.94, "algiz"), (0.66, "fehu"), (0.4, "sowilo"))):
        for j, (x0, z0, x1, z1) in enumerate(GLYPHS[kind]):
            x0, x1 = x0 * 0.82, x1 * 0.82
            rod("rune_%d_%d" % (k, j), (x0, face(x0) - 0.01, z + z0 * 0.85), (x1, face(x1) - 0.01, z + z1 * 0.85),
                0.028, glow, verts=4, emit=e)
    # Snow on its top and its shoulder, and the whole stone leaning a little.
    snow_cap("cap", 0.3, 0.26, (-0.06, 0.0, H - 0.07), h=0.06)
    snow_cap("shoulder", 0.2, 0.26, (-0.28, 0.0, body_h + 0.1), h=0.06)
    group(before, rot=(0, 0.05, 0))
    mound("drift", 0.5, (0, 0.02, 0), "fr_snow", (1.35, 0.95, 0.34))
    mound("drift_b", 0.24, (0.42, -0.2, 0), "fr_snow_sh", (1.2, 1.0, 0.55))
    for k, (x, y, h, lean) in enumerate(((-0.5, -0.12, 0.34, (0.2, -0.35, 0)), (-0.4, -0.3, 0.22, (-0.1, 0.3, 0)),
                                         (0.52, 0.0, 0.28, (0.15, 0.4, 0)))):
        ice_shard("crystal_%d" % k, h, 0.07, (x, y, 0.02), lean, "fr_ice" if k % 2 else "fr_ice_pale")
    return 2.25


# =================================================================================
#  The trapper's cabin, out on the lake
# =================================================================================

def pelt(name, x, y, z, w, h, colour, belly, dark=None):
    """A hide stretched flat and facing the camera: the body, a leg at each
    corner splayed out the way a skin is pegged, the neck at the top, the
    tail at the bottom, and the paler belly down the middle."""
    blk(name, (w, 0.03, h), (x, y, z), colour, bev=0.01)
    blk(name + "_belly", (w * 0.46, 0.035, h * 0.72), (x, y - 0.006, z - h * 0.02), belly, bev=0.01)
    for sx in (-1, 1):
        for sz in (-1, 1):
            blk("%s_leg_%d_%d" % (name, sx, sz), (w * 0.24, 0.03, h * 0.3),
                (x + sx * w * 0.52, y, z + sz * h * 0.4), colour, rot=(0, sx * sz * -0.75, 0), bev=0.01)
    blk(name + "_neck", (w * 0.36, 0.03, h * 0.22), (x, y, z + h * 0.58), colour, bev=0.01)
    blk(name + "_tail", (w * 0.14, 0.03, h * 0.34), (x, y, z - h * 0.64), dark or colour, bev=0.01)


def hoop_pelt(name, x, y, z, r, colour="fr_hide", belly="fr_hide_lt"):
    """A beaver skin laced into a round willow hoop, the trapper's sign."""
    torus(name + "_hoop", r, 0.035, (x, y, z), "fr_wood_lt", rot=(math.pi / 2, 0, 0))
    ob = cyl(name, r * 0.8, 0.04, (x, y - 0.01, z), colour, rot=(math.pi / 2, 0, 0), verts=16)
    ob.scale = (0.92, 1.12, 1.0)
    ob = cyl(name + "_in", r * 0.42, 0.05, (x, y - 0.02, z - r * 0.05), belly, rot=(math.pi / 2, 0, 0), verts=12)
    ob.scale = (0.9, 1.25, 1.0)
    for k in range(8):
        a = k / 8 * math.tau
        blk("%s_lace_%d" % (name, k), (0.03, 0.03, r * 0.22),
            (x + math.cos(a) * r * 0.9, y - 0.02, z + math.sin(a) * r * 0.9), "fr_rope", rot=(0, math.pi / 2 - a, 0),
            bev=0)
    blk(name + "_peg", (0.05, 0.06, 0.05), (x, y - 0.03, z + r + 0.02), "fr_wood_dk", bev=0)


def prop_trapper_cabin():
    """The trapper's cabin, on its islet in the middle of the frozen lake: a
    low hump of snow ringed with dark rock, so it reads as land and not as
    more lake; on it a cabin of round logs with their ends crossing at the
    corners, a steep roof under a thick coat of snow with icicles along its
    lip, a fieldstone chimney, one warm window, pelts stretched on the wall,
    and the door at the front centre, standing proud of the logs on a stone
    step under a little snow-laden canopy on brackets."""
    import random
    rng = random.Random(71)
    W, D = 3.8, 2.2
    R = 0.12                   # log radius -- chunky, or the courses vanish
    NC = 8
    H = NC * 2 * R
    base = 0.3
    floor = base + 0.06
    front = -D / 2

    # --- the islet ------------------------------------------------------------
    IY, IX, IR = 0.05, 2.95, 2.1
    mound("islet", 1.0, (0, IY, 0), "fr_snow", (IX, IR, base + 0.1))
    for k in range(30):
        a = k / 30 * math.tau + rng.uniform(-0.06, 0.06)
        if math.sin(a) > 0.6:
            continue            # behind the cabin: never seen
        x, y = math.cos(a) * IX, IY + math.sin(a) * IR
        size = (rng.uniform(0.2, 0.32), rng.uniform(0.16, 0.24), rng.uniform(0.13, 0.22))
        bp.rock("rock_%d" % k, size, (x, y, size[2] * 0.4), "fr_rock" if k % 3 else "fr_rock_dk", rng)
        if k % 2 == 0:
            snow_cap("rock_snow_%d" % k, size[0] * 1.5, size[1] * 1.4, (x, y + 0.02, size[2] * 1.25), h=0.06)
    blk("footing", (W + 0.1, D + 0.1, floor), (0, 0, floor / 2), "fr_stone_dk", bev=0.03)

    # --- the walls: round logs, the side walls' ends toward the camera -----------
    blk("core", (W - 0.1, D - 0.1, H), (0, 0, floor + H / 2), "fr_log_dk", bev=0)
    tones = ("fr_log", "fr_log_lt", "fr_log", "fr_log_dk")
    for k in range(NC):
        z = floor + R + k * 2 * R
        cyl("flog_%d" % k, R, W + 0.36, (0, front, z), tones[k % 4], rot=(0, math.pi / 2, 0), verts=12)
        for sx in (-1, 1):
            zs = z + R
            if zs > floor + H + 0.01:
                continue
            cyl("slog_%d_%d" % (k, sx), R, D + 0.36, (sx * W / 2, 0, zs), tones[(k + 2) % 4], rot=(math.pi / 2, 0, 0),
                verts=12)
            cyl("send_%d_%d" % (k, sx), R * 0.86, 0.03, (sx * W / 2, front - 0.18, zs), "fr_log_end",
                rot=(math.pi / 2, 0, 0), verts=12)
            cyl("sring_%d_%d" % (k, sx), R * 0.4, 0.035, (sx * W / 2, front - 0.185, zs), "fr_log_ring",
                rot=(math.pi / 2, 0, 0), verts=10)

    # --- the door, proud of the wall on its step ---------------------------------
    DW, DH = 0.86, 1.22
    dy = front - 0.2
    blk("step", (1.3, 0.5, floor - 0.12), (0, front - 0.36, 0.12 + (floor - 0.12) / 2), "fr_stone", bev=0.03)
    blk("step_snow_l", (0.2, 0.46, 0.05), (-0.56, front - 0.38, floor + 0.01), "fr_snow", bev=0.02)
    blk("door", (DW, 0.08, DH), (0, dy, floor + DH / 2), "fr_wood_lt")
    for i in range(4):
        blk("door_gap_%d" % i, (0.025, 0.02, DH - 0.06), (-DW / 2 + (i + 1) * DW / 5, dy - 0.045, floor + DH / 2),
            "fr_wood_dk", bev=0)
    for zz in (0.28, 0.94):
        blk("batten_%.2f" % zz, (DW - 0.1, 0.03, 0.08), (0, dy - 0.05, floor + zz), "fr_wood", bev=0)
    blk("brace", (0.07, 0.03, 0.8), (0, dy - 0.05, floor + 0.61), "fr_wood", rot=(0, 0.62, 0), bev=0)
    torus("ring", 0.06, 0.018, (0.28, dy - 0.07, floor + 0.62), "fr_iron", rot=(math.pi / 2, 0, 0))
    for sx in (-1, 1):
        blk("jamb_%d" % sx, (0.15, 0.2, DH + 0.1), (sx * (DW / 2 + 0.075), dy, floor + (DH + 0.1) / 2), "fr_log_dk")
    blk("lintel", (DW + 0.5, 0.22, 0.16), (0, dy, floor + DH + 0.16), "fr_log_dk")
    # The canopy over it: a little shed roof on two brackets, snow on it.
    cz = floor + H + 0.02
    for sx in (-1, 1):
        rod("bracket_%d" % sx, (sx * 0.72, front - 0.1, cz - 0.5), (sx * 0.72, front - 0.5, cz - 0.1), 0.04,
            "fr_log_dk", verts=6)
    blk("canopy", (1.7, 0.62, 0.07), (0, front - 0.28, cz - 0.02), "fr_shingle", rot=(0.24, 0, 0), bev=0.01)
    blk("canopy_snow", (1.66, 0.6, 0.1), (0, front - 0.26, cz + 0.06), "fr_snow", rot=(0.24, 0, 0), bev=0.04)
    icicles("canopy_ice", -0.78, 0.78, front - 0.58, cz - 0.1, 7, length=0.16, r=0.035, seed=3)

    # --- the lit window, on the left ------------------------------------------------
    wx, wz, wy = -1.25, floor + 0.98, front - 0.14
    blk("win_frame", (0.64, 0.08, 0.58), (wx, wy, wz), "fr_wood_dk")
    blk("win_glass", (0.5, 0.06, 0.44), (wx, wy - 0.03, wz), "fr_lit", emit=0.6, rough=0.3, bev=0)
    blk("win_mullion", (0.05, 0.07, 0.44), (wx, wy - 0.05, wz), "fr_wood_dk", bev=0)
    blk("win_transom", (0.5, 0.07, 0.05), (wx, wy - 0.05, wz + 0.04), "fr_wood_dk", bev=0)
    blk("win_sill", (0.76, 0.16, 0.06), (wx, wy - 0.06, wz - 0.32), "fr_wood", bev=0.01)
    blk("win_sill_snow", (0.72, 0.14, 0.06), (wx, wy - 0.06, wz - 0.27), "fr_snow", bev=0.02)
    for sx in (-1, 1):
        blk("shutter_%d" % sx, (0.16, 0.05, 0.6), (wx + sx * 0.42, wy - 0.02, wz), "fr_wood", bev=0.01)

    # --- pelts on the wall, on the right ----------------------------------------------
    hoop_pelt("beaver", 0.98, front - 0.16, floor + 1.0, 0.3)
    pelt("fox_skin", 1.52, front - 0.15, floor + 0.98, 0.3, 0.52, "fr_fur_red", "fr_fur_white")
    blk("fox_peg", (0.05, 0.06, 0.05), (1.52, front - 0.18, floor + 1.34), "fr_wood_dk", bev=0)

    # --- the roof: steep, under a thick coat of snow -----------------------------------
    eave = floor + H
    pitch = math.radians(48)
    over = 0.34
    half = D / 2 + over
    rise = half * math.tan(pitch)
    slab = half / math.cos(pitch)
    for side in (-1, 1):
        blk("roof_%d" % side, (W + 0.7, slab, 0.16), (0, side * half / 2, eave + rise / 2), "fr_shingle",
            rot=(-side * pitch, 0, 0), bev=0.02)
    ny, nz = -math.sin(pitch), math.cos(pitch)
    # The snow: a thick coat, narrower than the roof so the dark verges frame
    # it, stopping short of the eave so the roof's edge shows under its lip.
    sy = -half / 2 + 0.06 + ny * 0.17
    sz = eave + rise / 2 + 0.06 * math.tan(pitch) + nz * 0.17
    blk("roof_snow", (W + 0.4, slab - 0.2, 0.22), (0, sy, sz), "fr_snow", rot=(pitch, 0, 0), bev=0.07)
    # Soft ridges across it where it lies over the courses, and a few lumps,
    # so it reads as snow on a roof and not as a white board.
    for k, t in enumerate((0.3, 0.55, 0.8)):
        y = -half * (1 - t) + ny * 0.3
        z = eave + rise * t + nz * 0.3
        cyl("roof_ridge_%d" % k, 0.07, W + 0.3, (0, y, z), "fr_snowball", rot=(0, math.pi / 2, 0), verts=10)
    for k in range(5):
        t = rng.uniform(0.2, 0.8)
        x = rng.uniform(-W / 2 + 0.3, W / 2 - 0.3)
        lump = blob("roof_lump_%d" % k, 0.2, (x, -half * (1 - t) + ny * 0.3, eave + rise * t + nz * 0.3),
                    "fr_snowball", (1.6, 0.8, 0.3))
        lump.rotation_euler = (pitch, 0, 0)
    # Bargeboards: dark logs down the verges, so the roof has an edge.
    for sx in (-1, 1):
        blk("barge_%d" % sx, (0.16, slab + 0.04, 0.2), (sx * (W / 2 + 0.28), -half / 2 + ny * 0.12, eave + rise / 2
            + nz * 0.12), "fr_log_dk", rot=(pitch, 0, 0), bev=0.02)
    cyl("snow_lip", 0.13, W + 0.4, (0, -half + 0.16 + ny * 0.2, eave + 0.16 * math.tan(pitch) + nz * 0.2), "fr_snow",
        rot=(0, math.pi / 2, 0), verts=12)
    icicles("eave_ice", -W / 2 - 0.25, W / 2 + 0.25, -half - 0.02, eave - 0.06, 16, length=0.3, r=0.045, seed=5)
    cyl("ridge_snow", 0.17, W + 0.5, (0, 0.0, eave + rise + 0.1), "fr_snow", rot=(0, math.pi / 2, 0), verts=12)
    # The gable ends: log triangles, seen only as the roof's thickness edge-on,
    # but they close the silhouette where the slabs meet the walls.
    for sx in (-1, 1):
        for k in range(6):
            t = k / 6
            blk("gable_%d_%d" % (sx, k), (0.22, D * (1 - t) * 0.96, rise / 6),
                (sx * W / 2, 0, eave + rise * (t + 0.08)), "fr_log_dk" if k % 2 else "fr_log", bev=0.01)

    # --- a fieldstone chimney, on the front slope so it does not float ----------------
    cx, cyy = 1.2, -0.38
    z = eave + rise * (1 - abs(cyy) / half) - 0.3
    top = eave + rise + 0.5
    k = 0
    while z < top:
        for j in range(2):
            blk("chim_%d_%d" % (k, j), (0.28 + rng.random() * 0.04, 0.5, 0.17),
                (cx - 0.14 + j * 0.28 + (0.04 if k % 2 else 0.0), cyy, z), ("fr_stone", "fr_stone_lt")[(k + j) % 2],
                bev=0.03)
        z += 0.18
        k += 1
    blk("chim_cap", (0.7, 0.62, 0.08), (cx, cyy, top + 0.02), "fr_stone_dk")
    blk("chim_flue", (0.3, 0.26, 0.04), (cx, cyy, top + 0.07), "fr_void", bev=0)
    snow_cap("chim_snow", 0.36, 0.5, (cx - 0.16, cyy + 0.05, top + 0.08), h=0.08)
    for k, (dx, dz, r) in enumerate(((0.06, 0.3, 0.12), (0.24, 0.58, 0.15))):
        blob("smoke_%d" % k, r, (cx + dx, cyy + 0.1, top + dz), "fr_snow_dk", (1.3, 1.0, 0.8))

    # --- snow drifted against the walls ------------------------------------------------
    for k, (x, r, sq) in enumerate(((-1.95, 0.42, 0.75), (1.95, 0.4, 0.7), (-0.9, 0.3, 0.5), (1.25, 0.3, 0.45))):
        mound("drift_%d" % k, r, (x, front - 0.2, base - 0.05), "fr_snow" if k % 2 else "fr_snowball", (1.6, 0.9, sq))
    return (7.0, BUILDING)


# =================================================================================
#  The barrows
# =================================================================================

def menhir(name, x, y, h, w, t, rng, colour="fr_stone", lean=(0.0, 0.0)):
    """A standing stone: a seven-sided prism knocked out of true -- every
    corner pushed in or out a little and the top broken off at a slant --
    flat shaded, so it reads as a rough slab and not as a turned post."""
    bpy.ops.mesh.primitive_cylinder_add(radius=1.0, depth=h, location=(0, 0, 0), vertices=7)
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(bp.material(name, colour, 0.9))
    slant = rng.uniform(-0.25, 0.25)
    for v in ob.data.vertices:
        v.co.x *= w * rng.uniform(0.82, 1.12)
        v.co.y *= t * rng.uniform(0.82, 1.12)
        if v.co.z > 0:
            v.co.z += v.co.x * slant + rng.uniform(-0.06, 0.04)
            v.co.x *= 0.78
            v.co.y *= 0.8
    for poly in ob.data.polygons:
        poly.use_smooth = False
    ob.location = (x, y, h / 2)
    ob.rotation_euler = (lean[0], lean[1], rng.uniform(-0.3, 0.3))
    return ob


def prop_frost_barrow():
    """A long burial mound under the snow, a kerb of stones showing at its
    foot, rocks and dead grass poking through it; at its front a doorway
    framed by two rough jamb stones and a capstone, the way in sealed by a
    great slab set in the opening with runes cut in it and a dark joint all
    round it. Two tall standing stones stand guard either side, rimed and
    capped with snow. Decorative: nobody is meant to go in."""
    import random
    rng = random.Random(72)
    CY, RX, RY, MH = 0.72, 2.2, 1.3, 1.5
    earth_skirt("foot", CY, RX, RY, rng, n=6)
    mound("mound", 1.0, (0, CY + 0.04, 0), "fr_snow", (RX - 0.1, RY - 0.06, MH))
    # Long humps along its back, so its outline is ground and not a dish.
    for k, (x, y, rx, h) in enumerate(((-1.1, 1.05, 0.8, 1.54), (1.0, 1.1, 0.75, 1.5), (0.05, 1.3, 0.9, 1.66))):
        mound("hump_%d" % k, 1.0, (x, y, 0), "fr_snow" if k % 2 else "fr_snowball", (rx, 0.75, h))
    # The kerb: stones along the foot of the mound, standing out of the earth.
    for k in range(14):
        a = math.pi + (k + 0.5) / 14 * math.pi
        x, y = math.cos(a) * (RX + 0.02), CY + math.sin(a) * (RY + 0.0)
        if abs(x) < 1.05:
            continue
        bp.rock("kerb_%d" % k, (0.2, 0.14, 0.2), (x, y, 0.1), "fr_stone_dk" if k % 2 else "fr_stone", rng)
        snow_cap("kerb_snow_%d" % k, 0.28, 0.22, (x, y + 0.04, 0.26), h=0.05)
    # Stones showing through the snow on its flanks.
    for k, (x, y) in enumerate(((-1.55, 0.55), (1.6, 0.6), (-0.7, 0.95), (0.8, 0.85))):
        z = on_mound(x, y, CY, RX - 0.1, RY - 0.06, MH)
        bp.rock("stone_%d" % k, (0.3, 0.2, 0.18), (x, y - 0.1, z - 0.02), "fr_stone" if k % 2 else "fr_stone_lt", rng)
    # The doorway: rough jambs, a capstone, and the seal set in the opening
    # behind their faces, with a dark joint round it.
    face_y = -0.62
    OW, OH = 0.86, 1.02
    blk("seal_gap", (OW + 0.12, 0.12, OH + 0.08), (0, face_y + 0.06, (OH + 0.08) / 2), "fr_void", bev=0)
    blk("seal", (OW, 0.14, OH), (0, face_y - 0.02, OH / 2), "fr_stone_lt", bev=0.05)
    rune_glyph("seal_rune_a", -0.14, face_y - 0.1, OH * 0.55, "algiz", s=1.6, colour="fr_stone_dk", emit=0.0)
    rune_glyph("seal_rune_b", 0.16, face_y - 0.1, OH * 0.55, "tiwaz", s=1.6, colour="fr_stone_dk", emit=0.0)
    for sx in (-1, 1):
        ob = bp.rock("jamb_%d" % sx, (0.2, 0.24, OH / 2 + 0.1), (sx * (OW / 2 + 0.17), face_y - 0.06, OH / 2),
                     "fr_stone", rng)
        ob.rotation_euler = (0.04, sx * -0.05, sx * 0.2)
        clip_below(ob)
    ob = bp.rock("capstone", (0.98, 0.34, 0.2), (0.03, face_y + 0.02, OH + 0.26), "fr_stone_dk", rng)
    ob.rotation_euler = (0.03, 0.03, 0.02)
    snow_cap("cap_snow", 0.9, 0.36, (-0.12, face_y + 0.16, OH + 0.42), h=0.05)
    mound("cap_turf", 0.5, (0, face_y + 0.65, 0.0), "fr_snow", (1.4, 1.0, 3.1))
    # The facade: slabs set on end either side, shorter the further out.
    for sx in (-1, 1):
        for k in range(3):
            x = sx * (0.95 + k * 0.36)
            h = 0.92 - k * 0.2
            y = face_y + 0.04 - k * k * 0.05
            ob = bp.rock("slab_%d_%d" % (sx, k), (0.17, 0.13, h / 2), (x, y, h / 2 - 0.05),
                         ("fr_stone", "fr_stone_dk", "fr_stone_lt")[k], rng)
            ob.rotation_euler = (0.05, sx * 0.08 * k, rng.uniform(-0.2, 0.2))
            snow_cap("slab_snow_%d_%d" % (sx, k), 0.24, 0.2, (x, y, h - 0.07), h=0.05)
    # The two standing stones, guarding it, leaning a little apart.
    for sx in (-1, 1):
        x, y = sx * 1.32, -1.05
        menhir("guard_%d" % sx, x, y, 1.8, 0.27, 0.2, rng, lean=(0.0, sx * 0.06))
        snow_cap("guard_snow_%d" % sx, 0.24, 0.2, (x + sx * 0.08, y, 1.72), h=0.05)
        snow_cap("guard_rime_%d" % sx, 0.26, 0.08, (x + sx * 0.04, y - 0.19, 1.1), h=0.04, colour="fr_ice_pale")
        mound("guard_drift_%d" % sx, 0.26, (x + sx * 0.05, y - 0.05, 0), "fr_snow", (1.3, 0.9, 0.45))
    return (5.5, BUILDING)


# =================================================================================
#  The Warlord's Howe
# =================================================================================

def prop_howe_hall():
    """The Warlord's Howe: a great barrow under the snow, and in the front of
    it a built entrance -- two huge standing stones with runes cut in them
    and lit, a massive lintel across them with a stag's skull and its antlers
    set over it and skulls along its face, and between the stones an open,
    dark doorway that steps go down to, between kerbs, from a flagged
    forecourt. Walls of dry stone run out either side, holding back the
    mound, and an iron brazier of pale blue fire stands at each side of the
    steps. The doorway is at the image's horizontal centre."""
    import random
    rng = random.Random(83)
    MY, MRX, MRY, MH = 2.3, 4.8, 3.0, 3.5
    earth_skirt("foot", MY, MRX, MRY, rng, n=16, grass=22)
    mound("howe", 1.0, (0, MY, 0), "fr_snow", (MRX, MRY, MH))
    # Long low swells on its flanks, not round lumps: round ones read as bubbles.
    for k, (x, y, sx_, sq) in enumerate(((-2.6, 2.0, 2.0, 2.3), (2.6, 2.1, 2.0, 2.2))):
        mound("swell_%d" % k, 1.0, (x, y, 0), "fr_snowball", (sx_, 2.2, sq))
    # Long humps along the crest, so the outline is ground and not a bowl.
    for k, (x, y, rx, h) in enumerate(((-2.1, 2.5, 1.5, 3.4), (2.2, 2.4, 1.4, 3.3), (0.2, 3.2, 1.6, 3.85))):
        mound("crest_%d" % k, 1.0, (x, y, 0), "fr_snow", (rx, 1.4, h))
    # Stones showing through the snow, set into its surface so they show.
    for k, (x, y) in enumerate(((-2.7, 0.9), (2.9, 1.1), (-1.3, 1.5), (1.6, 1.7), (-3.6, 1.2), (3.6, 1.3),
                                (-2.2, 2.4), (2.4, 2.6), (0.4, 2.3))):
        z = on_mound(x, y, MY, MRX, MRY, MH)
        bp.rock("rock_%d" % k, (0.4, 0.28, 0.22), (x, y - 0.12, z - 0.04), "fr_stone_dk" if k % 2 else "fr_rock", rng)
        snow_cap("rock_snow_%d" % k, 0.46, 0.34, (x + 0.06, y - 0.05, z + 0.16), h=0.05)

    OW = 1.8                   # the doorway's width
    OH = 2.6                   # its top
    TH = -0.48                 # its threshold, below the ground: the steps go down to it
    FY = -1.3                  # the face of the standing stones
    JW, JD = 0.95, 0.9

    # A passage roof pushing out of the mound to the lintel, under the snow.
    mound("passage", 1.0, (0, 0.8, 0), "fr_snow", (2.2, 1.7, 3.95))

    # --- the walls of dry stone either side ----------------------------------------------
    for s in (-1, 1):
        for k in range(5):
            x = s * (OW / 2 + JW + 0.28 + k * 0.52)
            y = FY + 0.35 + k * k * 0.06
            h = 2.1 - k * 0.3
            n = max(2, int(round(h / 0.34)))
            for j in range(n):
                blk("wall_%d_%d_%d" % (s, k, j), (0.5 + rng.uniform(-0.04, 0.04), 0.55, h / n - 0.035),
                    (x + rng.uniform(-0.03, 0.03), y, (j + 0.5) * h / n),
                    ("fr_stone", "fr_stone_dk", "fr_stone_lt")[(j + k) % 3], rot=(0, 0, s * 0.1 * k), bev=0.035)
            blk("wall_snow_%d_%d" % (s, k), (0.58, 0.6, 0.08), (x, y + 0.02, h + 0.02), "fr_snow",
                rot=(0, 0, s * 0.1 * k), bev=0.035)

    # --- the standing stones and the lintel -------------------------------------------------
    for s in (-1, 1):
        jx = s * (OW / 2 + JW / 2)
        blk("jamb_%d" % s, (JW, JD, OH + 0.3), (jx, FY + JD / 2, (OH + 0.3) / 2), "fr_stone", rot=(0, s * -0.02, 0),
            bev=0.1)
        blk("jamb_face_%d" % s, (JW - 0.3, 0.04, OH - 0.5), (jx, FY - 0.01, (OH + 0.3) / 2), "fr_stone_dk", bev=0)
        for k, kind in enumerate(("tiwaz", "algiz", "sowilo")):
            rune_glyph("jrune_%d_%d" % (s, k), jx, FY - 0.04, 0.75 + k * 0.6, kind, s=1.5)
        snow_cap("jamb_foot_%d" % s, 0.9, 0.5, (jx, FY - 0.1, 0.0), h=0.14)
    LZ = OH + 0.3
    blk("lintel", (OW + 2 * JW + 0.7, 1.1, 0.9), (0, FY + 0.5, LZ + 0.45), "fr_stone_lt", bev=0.1)
    blk("lintel_band", (OW + 2 * JW + 0.4, 0.04, 0.16), (0, FY - 0.06, LZ + 0.12), "fr_stone_dk", bev=0)
    blk("lintel_snow", (OW + 2 * JW + 0.5, 0.9, 0.1), (0, FY + 0.6, LZ + 0.93), "fr_snow", bev=0.05)
    for k, (x, w) in enumerate(((-1.2, 0.8), (0.9, 1.0))):
        snow_cap("lintel_drift_%d" % k, w, 0.6, (x, FY + 0.7, LZ + 0.97), h=0.07)

    # --- the doorway, open and dark, and the steps down to it --------------------------------
    blk("dark", (OW + 0.1, 1.4, OH - TH + 0.2), (0, FY + 0.75, (OH + TH) / 2), "fr_void", bev=0)
    N, run = 4, 0.3
    y0 = FY - N * run
    tread_col = ("fr_stone_lt", "fr_stone_lt", "fr_stone", "fr_stone")
    for k in range(N):
        zt = TH * (k + 1) / N
        yc = y0 + (k + 0.5) * run
        blk("tread_%d" % k, (OW, run + 0.02, 0.3), (0, yc, zt - 0.15), tread_col[k], bev=0.015)
        blk("nosing_%d" % k, (OW, 0.05, 0.03), (0, y0 + k * run + 0.03, zt + 0.005), "fr_rock_dk", bev=0)
        for s in (-1, 1):
            blk("kerb_%d_%d" % (s, k), (0.36, run + 0.02, 0.5 - zt), (s * (OW / 2 + 0.18), yc, (0.5 + zt) / 2 - 0.02),
                ("fr_stone", "fr_stone_dk")[(k + (s > 0)) % 2], bev=0.03)
    blk("threshold", (OW, 0.4, 0.12), (0, FY + 0.15, TH - 0.06), "fr_stone_dk", bev=0.01)
    for s in (-1, 1):
        blk("kerb_snow_%d" % s, (0.4, N * run + 0.02, 0.07), (s * (OW / 2 + 0.18), (y0 + FY) / 2, 0.5), "fr_snow",
            bev=0.03)

    # --- the forecourt, flagged, half under snow ----------------------------------------------
    for i in range(7):
        for j in range(2):
            x = -2.7 + i * 0.9 + (0.45 if j else 0.0)
            if abs(x) > 3.0:
                continue
            blk("flag_%d_%d" % (i, j), (0.84, 0.28, 0.06), (x, y0 - 0.16 - j * 0.3, 0.03),
                "fr_stone" if (i + j) % 2 else "fr_stone_lt", rot=(0, 0, rng.uniform(-0.04, 0.04)), bev=0.02)
    for k, (x, r) in enumerate(((-2.6, 0.5), (2.5, 0.45), (-1.4, 0.3), (1.6, 0.34))):
        mound("court_snow_%d" % k, r, (x, y0 - 0.3, 0), "fr_snow", (1.6, 0.8, 0.22))

    # --- the trophies over the lintel ------------------------------------------------------------
    elk_skull("stag", 0, FY - 0.08, LZ + 0.62, s=1.5)
    antlers("rack", 0, FY + 0.15, LZ + 0.86, s=1.45, wide=1.1, tall=0.9)
    for k, x in enumerate((-1.75, -1.1, 1.1, 1.75)):
        skull("lskull_%d" % k, x, FY - 0.06, LZ + 0.45, 1.25)
    # --- and a brazier at each side of the steps -------------------------------------------------
    for s in (-1, 1):
        brazier("brazier_%d" % s, s * 2.1, y0 - 0.25, 1.3)
    return (11.0, BUILDING)


# =================================================================================
#  In the Howe
# =================================================================================

def prop_draugr_throne():
    """The high seat of the dead lord: a block seat on a stepped dais, a tall
    back with a carved border and runes lit in it, heavy arms ending in
    skulls, a stag's antlers laid across the top of the back, frost on every
    top and icicles off every edge, and skulls heaped at its feet."""
    Y = -0.25
    blk("dais", (2.7, 1.8, 0.2), (0, Y + 0.2, 0.1), "fr_stone_dk", bev=0.03)
    blk("dais2", (2.2, 1.35, 0.2), (0, Y + 0.38, 0.3), "fr_stone", bev=0.03)
    top = 0.4
    blk("seat", (1.36, 0.95, 0.5), (0, Y + 0.45, top + 0.25), "fr_stone", bev=0.04)
    blk("seat_top", (1.42, 1.0, 0.08), (0, Y + 0.45, top + 0.52), "fr_stone_lt", bev=0.02)
    blk("seat_band", (1.1, 0.04, 0.12), (0, Y - 0.04, top + 0.25), "fr_stone_dk", bev=0)
    for k in range(4):
        blk("seat_notch_%d" % k, (0.06, 0.05, 0.12), (-0.42 + k * 0.28, Y - 0.05, top + 0.25), "fr_stone_lt", bev=0)
    BH = 1.6
    by = Y + 0.85
    blk("back", (1.5, 0.36, BH), (0, by, top + BH / 2), "fr_stone", bev=0.05)
    for s in (-1, 1):
        blk("back_peak_%d" % s, (0.86, 0.36, 0.22), (s * 0.36, by, top + BH + 0.06), "fr_stone", rot=(0, s * 0.42, 0),
            bev=0.04)
        blk("border_%d" % s, (0.12, 0.04, BH - 0.25), (s * 0.6, by - 0.19, top + BH / 2 + 0.05), "fr_stone_lt", bev=0)
    blk("border_top", (1.3, 0.04, 0.12), (0, by - 0.19, top + BH - 0.1), "fr_stone_lt", bev=0)
    for k, (a, z) in enumerate(((0.6, 0.95), (-0.6, 0.95), (0.6, 1.45), (-0.6, 1.45))):
        blk("knot_%d" % k, (0.62, 0.04, 0.07), (0, by - 0.195, top + z - 0.2), "fr_stone_dk", rot=(0, a, 0), bev=0)
    for k, (x, kind) in enumerate(((-0.33, "algiz"), (0.33, "tiwaz"))):
        rune_glyph("brune_%d" % k, x, by - 0.21, top + 1.3, kind, s=1.0, emit=1.3)
    # The arms, on a post each, ending in skulls.
    for s in (-1, 1):
        ax = s * 0.82
        blk("arm_%d" % s, (0.28, 1.05, 0.2), (ax, Y + 0.42, top + 0.86), "fr_stone_lt", bev=0.03)
        blk("arm_post_%d" % s, (0.24, 0.24, 0.8), (ax, Y + 0.0, top + 0.4), "fr_stone", bev=0.03)
        blk("arm_side_%d" % s, (0.22, 0.8, 0.5), (ax, Y + 0.5, top + 0.52), "fr_stone_dk", bev=0.03)
        skull("arm_skull_%d" % s, ax, Y - 0.1, top + 1.06, 0.85)
        icicles("arm_ice_%d" % s, ax - 0.1, ax + 0.1, Y - 0.12, top + 0.74, 2, length=0.2, r=0.04, seed=s + 2)
    # Antlers laid across the top of the back, a stag's skull where they meet.
    antlers("rack", 0, by + 0.05, top + BH + 0.05, s=1.0, wide=1.45, tall=0.62)
    elk_skull("stag", 0, by - 0.22, top + BH - 0.05, s=0.85)
    # Frost and icicles.
    icicles("seat_ice", -0.6, 0.6, Y - 0.05, top + 0.48, 7, length=0.24, r=0.045, seed=7)
    icicles("dais_ice", -1.0, 1.0, Y + 0.38 - 0.69, top - 0.02, 8, length=0.14, r=0.035, seed=9)
    for k, (x, y, z, w) in enumerate(((0.3, Y + 0.5, top + 0.57, 0.5), (-0.82, Y + 0.5, top + 0.97, 0.22),
                                      (0.82, Y + 0.3, top + 0.97, 0.22), (-0.4, by, top + BH + 0.1, 0.5))):
        snow_cap("frost_%d" % k, w, 0.3, (x, y, z), h=0.04, colour="fr_ice_pale")
    for k, (x, y, r) in enumerate(((-1.25, Y - 0.55, 0.3), (1.2, Y - 0.6, 0.26), (1.15, Y + 1.0, 0.3),
                                   (-1.2, Y + 0.95, 0.28))):
        mound("drift_%d" % k, r, (x, y, 0), "fr_snow", (1.4, 1.0, 0.6))
    # Skulls at its feet: on the step and on the floor before it.
    for k, (x, y, z, s) in enumerate(((-0.55, Y - 0.45, 0.33, 1.0), (0.62, Y - 0.4, 0.33, 0.9),
                                      (-0.9, Y - 0.85, 0.12, 1.05), (0.1, Y - 0.95, 0.12, 1.1),
                                      (0.85, Y - 0.9, 0.12, 0.95), (-0.25, Y - 0.62, 0.3, 0.85))):
        skull("foot_skull_%d" % k, x, y, z, s)
    return 4.0


def prop_stone_coffin():
    """A stone coffin on a low plinth, its lid carved with the warrior in it
    lying at rest -- a helmed head on a stone pillow, his hands on the hilt
    of a sword laid down his body, his feet up -- and a band of runes cut
    along its side. Rime on its edges and short icicles off the lid."""
    L, Dd = 2.1, 0.9
    blk("plinth", (L + 0.24, Dd + 0.2, 0.14), (0, 0, 0.07), "fr_stone_dk", bev=0.03)
    blk("chest", (L, Dd, 0.62), (0, 0, 0.14 + 0.31), "fr_stone", bev=0.03)
    # The side: a raised frame, three panels, a rune band cut in the middle one.
    fy = -Dd / 2 - 0.015
    for z in (0.22, 0.68):
        blk("frame_h_%.2f" % z, (L - 0.1, 0.04, 0.06), (0, fy, z), "fr_stone_lt", bev=0)
    for x in (-0.98, -0.34, 0.34, 0.98):
        blk("frame_v_%.2f" % x, (0.06, 0.04, 0.5), (x, fy, 0.45), "fr_stone_lt", bev=0)
    for k in range(5):
        x = -0.24 + k * 0.12
        blk("rune_s_%d" % k, (0.03, 0.03, 0.22), (x, fy - 0.01, 0.45), "fr_stone_dk", bev=0)
        blk("rune_b_%d" % k, (0.03, 0.03, 0.1), (x + 0.03, fy - 0.01, 0.5 - 0.04 * (k % 2)), "fr_stone_dk",
            rot=(0, 0.7 if k % 2 else -0.7, 0), bev=0)
    for sx in (-1, 1):
        torus("knot_%d" % sx, 0.13, 0.025, (sx * 0.66, fy - 0.01, 0.45), "fr_stone_dk", rot=(math.pi / 2, 0, 0))
    blk("lid", (L + 0.14, Dd + 0.14, 0.14), (0, 0, 0.76 + 0.07), "fr_stone", bev=0.04)
    lz = 0.9
    # The effigy, lying along the lid, head to the left.
    E = "fr_effigy"
    blk("pillow", (0.34, 0.52, 0.1), (-0.8, 0.02, lz + 0.05), "fr_stone_dk", bev=0.02)
    blob("head", 0.18, (-0.78, 0.0, lz + 0.2), E, (1.0, 1.0, 0.9))
    blk("helm_rim", (0.1, 0.38, 0.1), (-0.7, 0.0, lz + 0.17), E, bev=0.02)
    blk("nasal", (0.14, 0.06, 0.05), (-0.64, -0.0, lz + 0.3), E, bev=0.01)
    blk("shoulders", (0.24, 0.56, 0.22), (-0.5, 0.0, lz + 0.12), E, bev=0.07)
    blk("torso", (0.5, 0.48, 0.2), (-0.2, 0.0, lz + 0.11), E, bev=0.07)
    blk("belt", (0.08, 0.5, 0.21), (0.02, 0.0, lz + 0.11), "fr_stone_dk", bev=0.02)
    for sy in (-1, 1):
        blk("leg_%d" % sy, (0.66, 0.2, 0.15), (0.36, sy * 0.11, lz + 0.09), E, bev=0.05)
        blk("foot_%d" % sy, (0.12, 0.15, 0.26), (0.72, sy * 0.11, lz + 0.15), E, bev=0.03)
        blob("hand_%d" % sy, 0.075, (-0.34, sy * 0.07, lz + 0.26), E)
    blk("blade", (0.92, 0.09, 0.06), (0.14, -0.0, lz + 0.24), "fr_bone", bev=0.01)
    blk("guard", (0.06, 0.32, 0.07), (-0.3, 0.0, lz + 0.26), "fr_stone_dk", bev=0.01)
    blk("grip", (0.14, 0.06, 0.06), (-0.4, 0.0, lz + 0.27), "fr_stone_dk", bev=0.01)
    blob("pommel", 0.06, (-0.5, 0.0, lz + 0.27), "fr_stone_dk")
    # Rime and icicles.
    for k, (x, y, w, d) in enumerate(((-0.95, -0.4, 0.3, 0.18), (0.9, 0.38, 0.34, 0.2), (0.5, -0.44, 0.4, 0.14),
                                      (-1.02, 0.36, 0.22, 0.2))):
        snow_cap("rime_%d" % k, w, d, (x, y, lz + 0.01), h=0.035, colour="fr_ice_pale")
    icicles("lid_ice", -0.9, 0.9, -Dd / 2 - 0.08, 0.76, 9, length=0.14, r=0.035, seed=4)
    for k, (x, y, r) in enumerate(((-1.0, -0.5, 0.22), (1.02, -0.5, 0.2), (1.0, 0.55, 0.22))):
        mound("drift_%d" % k, r, (x, y, 0), "fr_snow", (1.3, 1.0, 0.6))
    # From higher than the furniture's camera: at 34 degrees the lid is a
    # sliver and the man on it is a grey lump.
    return (3.0, 44.0)


# =================================================================================
#  The Abominable Snowman's totem
# =================================================================================

def prop_totem_abominable():
    """The Abominable Snowman's totem: a pale post banded in ice-blue, and on
    top a small shaggy white head -- a blue-grey face, pale lit eyes, two
    fangs, and the fur stood up round it in tufts."""
    r = bp._totem("tt_yeti", "tt_yeti_b", "none", eye_emit=1.6)
    top = 1.02
    blob("yeti", 0.23, (0, 0.05, top + 0.17), "fr_fur_white", (1.25, 1.0, 1.0))
    # Shag hanging off the cheeks, pointing down, so the outline is fur and
    # not the points of a crown.
    for k, (sx, z, lean) in enumerate(((-1, 0.1, 0.7), (1, 0.1, 0.7), (-1, 0.24, 1.0), (1, 0.24, 1.0))):
        spike("shag_%d" % k, (sx * 0.24, 0.02, top + z + 0.06), (sx * (0.24 + 0.1 * lean), 0.0, top + z - 0.12), 0.07,
              "fr_fur_white", verts=5)
    # The face: a dark plate standing out of the fur, lit eyes, a white brow
    # over it and two fangs under it.
    # (Named apart from _totem's own "brow" and "eye": materials are cached by
    # name, and a second "brow" would come out in the band's blue.)
    blk("yeti_face", (0.32, 0.1, 0.24), (0, -0.17, top + 0.13), "fr_yeti_face", bev=0.04)
    blk("yeti_brow", (0.38, 0.1, 0.08), (0, -0.19, top + 0.27), "fr_fur_white", bev=0.02)
    for sx in (-1, 1):
        blk("yeti_eye_%d" % sx, (0.07, 0.03, 0.05), (sx * 0.07, -0.23, top + 0.17), "tt_yeti_b", emit=2.0, bev=0)
        spike("yeti_fang_%d" % sx, (sx * 0.06, -0.21, top + 0.07), (sx * 0.06, -0.23, top - 0.03), 0.03, "fr_fur_white",
              verts=4)
    return r


# =================================================================================
#  Registration: name -> (builder, final pixels across)
# =================================================================================
PROPS = {
    "snowman":          (prop_snowman, 56),
    "igloo":            (prop_igloo, 128),
    "pelt_rack":        (prop_pelt_rack, 72),
    "woodpile":         (prop_woodpile, 64),
    "broken_sled":      (prop_broken_sled, 72),
    "ice_hole":         (prop_ice_hole, 48),
    "frost_brazier":    (prop_frost_brazier, 64),
    "runestone":        (prop_runestone, 72),
    "trapper_cabin":    (prop_trapper_cabin, 224),
    "frost_barrow":     (prop_frost_barrow, 176),
    "howe_hall":        (prop_howe_hall, 352),
    "draugr_throne":    (prop_draugr_throne, 128),
    "stone_coffin":     (prop_stone_coffin, 96),
    "totem_abominable": (prop_totem_abominable, 32),
}
