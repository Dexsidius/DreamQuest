# =============================================================================
#  blender_primordium_props.py - the Primordium, where the elements were before
#  there was a world to put them in: the rift at the Stronghold that opens on
#  it, the five maps' standing things and the wellspring at the heart of each,
#  the dais at the Conflux, and the Quintessence's totem.
#
#  Rendered by tools/make_props.ps1 like every other prop:
#      .\tools\make_props.ps1 -Only magma_well,kiln_vent
#
#  Built with blender_props.py's own tools and registered into its PROPS
#  table, the way blender_frostreach_props.py is: that file hands itself over.
#
#  One Blender unit is about one 32px cell. Each map is one element, and each
#  must be told from the others at a glance, so each keeps to its own few
#  colours and nothing else: the Kiln is black glass and basalt lit orange and
#  yellow; the Bedrock grey-brown stone with amber light and pale green
#  crystal; the Deeps coral rose and teal, pearl and sea-blue; the Firmament
#  pale stone, white cloud and pale blue; the Tempest slate and black with
#  pale yellow lightning. The rift and the Conflux carry all five.
#
#  The wellsprings and the dais are ground features: what matters in them is
#  the pool, the chasm or the star lying in the middle, so they are seen from
#  higher than the furniture (WELL and DAIS below).
# =============================================================================

import math
import random

import bmesh
import bpy
from mathutils import Euler, Vector

import blender_props as bp

blk, cyl, cone, sphere = bp.blk, bp.cyl, bp.cone, bp.sphere
BUILDING = bp.BUILDING_ELEVATION
WELL = 52.0          # the wellsprings: the pool shows, and what stands round it still stands
DAIS = 62.0          # the Conflux's dais: the star on it is the point of it

bp.PALETTE.update({
    # The Kiln: black basalt, black glass, and fire.
    "pm_basalt":      (0.150, 0.135, 0.135),
    "pm_basalt_dk":   (0.085, 0.075, 0.080),
    "pm_basalt_lt":   (0.260, 0.230, 0.220),
    "pm_ash":         (0.220, 0.200, 0.200),
    "pm_obsidian":    (0.120, 0.100, 0.140),
    "pm_obsidian_lt": (0.300, 0.270, 0.360),
    "pm_obsidian_hi": (0.480, 0.450, 0.560),
    "pm_crust":       (0.300, 0.090, 0.050),
    "pm_magma":       (1.000, 0.470, 0.080),
    "pm_magma_hot":   (1.000, 0.760, 0.220),
    "pm_magma_dk":    (0.820, 0.200, 0.040),
    "pm_flame_red":   (0.880, 0.200, 0.050),
    "pm_flame":       (1.000, 0.480, 0.100),
    "pm_flame_yel":   (1.000, 0.800, 0.260),
    "pm_flame_core":  (1.000, 0.960, 0.720),
    # The Bedrock: grey-brown stone, amber light, pale green crystal.
    "pm_earth":       (0.420, 0.370, 0.320),
    "pm_earth_dk":    (0.290, 0.250, 0.220),
    "pm_earth_lt":    (0.560, 0.500, 0.430),
    "pm_slab":        (0.270, 0.250, 0.240),
    "pm_slab_dk":     (0.180, 0.170, 0.170),
    "pm_slab_lt":     (0.380, 0.350, 0.330),
    "pm_amber":       (1.000, 0.600, 0.130),
    "pm_amber_hot":   (1.000, 0.850, 0.420),
    "pm_jade":        (0.600, 0.900, 0.580),
    "pm_jade_dk":     (0.330, 0.640, 0.400),
    "pm_topaz":       (1.000, 0.720, 0.280),
    "pm_topaz_dk":    (0.800, 0.460, 0.130),
    "pm_geode":       (0.600, 0.360, 0.840),
    "pm_geode_lt":    (0.820, 0.640, 0.990),
    "pm_geode_dk":    (0.340, 0.180, 0.520),
    "pm_rind":        (0.880, 0.860, 0.820),
    "pm_moss":        (0.380, 0.470, 0.240),
    "pm_moss_dk":     (0.260, 0.340, 0.170),
    # The Deeps: coral rose and teal, pearl, sea-blue.
    "pm_coral":       (0.930, 0.480, 0.540),
    "pm_coral_dk":    (0.700, 0.280, 0.380),
    "pm_coral_lt":    (1.000, 0.720, 0.720),
    "pm_teal":        (0.220, 0.660, 0.640),
    "pm_teal_lt":     (0.500, 0.860, 0.790),
    "pm_pearl":       (0.960, 0.930, 0.950),
    "pm_pearl_pink":  (0.980, 0.800, 0.840),
    "pm_pearl_blue":  (0.800, 0.900, 0.970),
    "pm_shell":       (0.950, 0.860, 0.720),
    "pm_shell_band":  (0.800, 0.560, 0.440),
    "pm_sea":         (0.120, 0.420, 0.680),
    "pm_sea_dk":      (0.060, 0.230, 0.430),
    "pm_sea_lt":      (0.360, 0.680, 0.900),
    "pm_tide":        (0.320, 0.940, 0.860),
    "pm_foam":        (0.930, 0.980, 1.000),
    "pm_kelp":        (0.200, 0.360, 0.150),
    "pm_kelp_olive":  (0.440, 0.450, 0.180),
    "pm_kelp_lt":     (0.520, 0.600, 0.230),
    "pm_kelp_float":  (0.620, 0.560, 0.240),
    "pm_seastone":    (0.360, 0.420, 0.470),
    "pm_seastone_dk": (0.240, 0.290, 0.340),
    "pm_seastone_lt": (0.520, 0.580, 0.620),
    "pm_sand":        (0.820, 0.760, 0.600),
    # The Firmament: pale stone, white cloud, pale blue.
    "pm_pale":        (0.840, 0.820, 0.780),
    "pm_pale_dk":     (0.660, 0.650, 0.650),
    "pm_pale_lt":     (0.940, 0.930, 0.890),
    "pm_pale_blue":   (0.660, 0.780, 0.900),
    "pm_sky":         (0.700, 0.860, 1.000),
    "pm_cloud":       (0.970, 0.980, 1.000),
    "pm_cloud_sh":    (0.800, 0.850, 0.930),
    "pm_pale_sh":     (0.540, 0.540, 0.570),
    "pm_dust":        (0.450, 0.430, 0.420),
    "pm_shadow":      (0.160, 0.180, 0.220),
    # The Tempest: slate and black, and pale yellow lightning.
    "pm_slate":       (0.270, 0.290, 0.340),
    "pm_slate_dk":    (0.150, 0.160, 0.200),
    "pm_slate_lt":    (0.400, 0.430, 0.490),
    "pm_storm":       (0.100, 0.100, 0.120),
    # Yellow enough to stay yellow once lit: paler, emission takes it to
    # near-white, and the game's night glow (prop.frag's Lit test wants a
    # bright pixel with some colour in it) passes over white.
    "pm_bolt":        (1.000, 0.900, 0.340),
    "pm_bolt_core":   (1.000, 1.000, 0.880),
    "pm_bolt_dk":     (0.950, 0.820, 0.320),
    "pm_fulg":        (0.380, 0.370, 0.420),
    "pm_fulg_lt":     (0.640, 0.620, 0.680),
    "pm_fulg_dk":     (0.200, 0.190, 0.230),
    "pm_fulg_glow":   (0.900, 0.860, 0.560),
    "pm_iron":        (0.260, 0.260, 0.290),
    "pm_iron_lt":     (0.440, 0.450, 0.490),
    "pm_rust":        (0.450, 0.280, 0.180),
    "pm_scorch":      (0.070, 0.065, 0.070),
    # The rift and the Conflux: the plateau's grey basalt, and the light of
    # the place between, which is every colour at once.
    "pm_basalt_grey": (0.420, 0.400, 0.400),
    "pm_basalt_gdk":  (0.260, 0.250, 0.260),
    "pm_basalt_glt":  (0.560, 0.540, 0.530),
    "pm_basalt_gmd":  (0.480, 0.460, 0.460),
    "pm_tear_rim":    (0.180, 0.080, 0.300),
    "pm_tear_violet": (0.560, 0.320, 0.920),
    "pm_tear_pink":   (0.930, 0.560, 1.000),
    "pm_tear_blue":   (0.640, 0.820, 1.000),
    "pm_tear_core":   (1.000, 0.980, 1.000),
    "pm_dais":        (0.520, 0.500, 0.540),
    "pm_dais_dk":     (0.300, 0.280, 0.320),
    "pm_dais_lt":     (0.700, 0.680, 0.700),
    "pm_gold":        (0.860, 0.700, 0.340),
    # One colour per element, where all five are set side by side: the rift's
    # stones, the dais's star, the totem's gems. Earth is the pale green of
    # its crystal there, not amber: beside fire's orange and lightning's
    # yellow, amber is a third shade of the same thing.
    "el_fire":        (1.000, 0.420, 0.100),
    "el_earth":       (0.560, 0.900, 0.520),
    "el_water":       (0.300, 0.640, 1.000),
    "el_air":         (0.940, 0.970, 1.000),
    "el_lightning":   (1.000, 0.930, 0.420),
    "tt_quint":       (0.110, 0.100, 0.130),
    "tt_quint_b":     (0.600, 0.400, 0.940),
})


# --- pieces -------------------------------------------------------------------
def _aim(p0, p1):
    """The rotation that points an object's +Z from p0 toward p1, and the length."""
    dx, dy, dz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    yaw = math.atan2(dx, -dy) if (dx or dy) else 0.0
    tilt = math.acos(max(-1.0, min(1.0, dz / length))) if length else 0.0
    return (tilt, 0.0, yaw), length


def _lerp(p, q, t):
    return (p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t, p[2] + (q[2] - p[2]) * t)


def flat(ob):
    """Flat shaded: every face its own band of colour, which is what makes a
    stone faceted and a crystal glassy at forty pixels."""
    for p in ob.data.polygons:
        p.use_smooth = False
    return ob


def rod(name, p0, p1, r, colour, verts=8, emit=0.0, rough=0.8):
    """A round length from one point to another: a branch, an arc, a vein."""
    rot, length = _aim(p0, p1)
    ob = cyl(name, r, length, _lerp(p0, p1, 0.5), colour, verts=verts, emit=emit, rough=rough)
    ob.rotation_euler = rot
    return ob


def spike(name, base, tip, r, colour, verts=8, emit=0.0, rough=0.8):
    """A cone from its base toward its point: a tongue of flame, a shard."""
    rot, length = _aim(base, tip)
    return cone(name, r, length, _lerp(base, tip, 0.5), colour, rot=rot, verts=verts, emit=emit, rough=rough)


def blob(name, r, loc, colour, scale=(1, 1, 1), emit=0.0):
    ob = sphere(name, r, loc, colour, emit=emit)
    ob.scale = scale
    return ob


def frustum(name, r1, r2, h, loc, colour, rot=(0, 0, 0), verts=12, emit=0.0, rough=0.8):
    """A cone cut off: a vent, a funnel, a stump."""
    bpy.ops.mesh.primitive_cone_add(radius1=r1, radius2=r2, depth=h, location=loc, vertices=verts)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = rot
    ob.data.materials.append(bp.material(name, colour, rough, 0.0, emit))
    return ob


def torus(name, major, minor, loc, colour, rot=(0, 0, 0), scale=(1, 1, 1), emit=0.0, segs=24):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, location=loc,
                                     major_segments=segs, minor_segments=8)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_euler = rot
    ob.scale = scale
    ob.data.materials.append(bp.material(name, colour, 0.8, 0.0, emit))
    return ob


def clip_below(ob, z0=0.0):
    """Cuts away everything of a mesh under z0, so a dome sat on the ground
    does not show its lower half under its front edge."""
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
    """A heap or a bank: a sphere squashed to size, with nothing of it under
    the ground."""
    return clip_below(blob(name, r, loc, colour, scale))


def jitter(ob, rng, amount):
    """Knocks every vertex of a mesh a little out of true, so a cone of rock
    is a heap of rock and not a turned one. The foot stays on the ground."""
    zmin = min(v.co.z for v in ob.data.vertices)
    for v in ob.data.vertices:
        v.co.x += rng.uniform(-amount, amount)
        v.co.y += rng.uniform(-amount, amount)
        if v.co.z > zmin + 1e-4:
            v.co.z += rng.uniform(-amount, amount) * 0.6
    return ob


def fire(name, x, y, z, r, h, emit=1.2, lean=(0.0, 0.0), tongues=5, seed=1):
    """A flame of the Kiln's: tongues of red and orange leaning out round a
    taller orange body, yellow toward the heart and a white-yellow core.
    Emission is kept low or the whole of it blows out into a yellow disc."""
    rng = random.Random(seed)
    lx, ly = lean
    spike(name, (x, y, z), (x + lx * h, y + ly * h, z + h), r * 0.62, "pm_flame", verts=9, emit=emit)
    for k in range(tongues):
        a = (k + 0.5) / tongues * math.tau + rng.uniform(-0.3, 0.3)
        hh = rng.uniform(0.5, 0.78)
        px, py = x + math.cos(a) * r * 0.55, y + math.sin(a) * r * 0.5
        tip = (px + math.cos(a) * r * 0.45 + lx * h * hh, py + math.sin(a) * r * 0.3 + ly * h * hh, z + h * hh)
        spike("%s_tongue_%d" % (name, k), (px, py, z), tip, r * 0.4, "pm_flame_red" if k % 2 else "pm_flame",
              verts=7, emit=emit)
    spike(name + "_mid", (x, y - r * 0.2, z), (x + lx * h * 0.8 + r * 0.05, y - r * 0.25 + ly * h * 0.8, z + h * 0.78),
          r * 0.46, "pm_flame_yel", verts=8, emit=emit)
    spike(name + "_core", (x, y - r * 0.38, z), (x + lx * h * 0.45, y - r * 0.38, z + h * 0.45), r * 0.3,
          "pm_flame_core", verts=7, emit=emit + 0.2)


def column_of_fire(name, x, y, z, r, h, emit=1.15):
    """A pillar of flame standing up out of a pool: one tall orange body,
    tongues rising round it nearly straight, a yellow heart and a white core,
    and a wisp or two torn off near the top. Narrow for its height, so seen
    from above it still stands rather than spreading into a flower."""
    rng = random.Random(len(name) * 7 + 3)
    spike(name, (x, y, z), (x + 0.04, y, z + h), r, "pm_flame", verts=10, emit=emit)
    n = 8
    for k in range(n):
        a = (k + 0.5) / n * math.tau
        hh = h * rng.uniform(0.35, 0.72)
        px, py = x + math.cos(a) * r * 0.72, y + math.sin(a) * r * 0.62
        spike("%s_tongue_%d" % (name, k), (px, py, z), (px + math.cos(a) * r * 0.25, py + math.sin(a) * r * 0.2,
              z + hh), r * 0.38, "pm_flame_red" if k % 2 else "pm_flame", verts=7, emit=emit)
    spike(name + "_mid", (x, y - r * 0.25, z), (x + 0.03, y - r * 0.3, z + h * 0.74), r * 0.62, "pm_flame_yel",
          verts=9, emit=emit)
    spike(name + "_core", (x, y - r * 0.45, z), (x, y - r * 0.5, z + h * 0.42), r * 0.36, "pm_flame_core", verts=8,
          emit=emit + 0.2)
    for k, (dx, zz, hh) in enumerate(((0.16, 0.78, 0.2), (-0.14, 0.9, 0.14))):
        spike("%s_wisp_%d" % (name, k), (x + dx, y - 0.05, z + h * zz), (x + dx * 1.3, y - 0.05, z + h * zz + h * hh),
              r * 0.22, "pm_flame_red" if k else "pm_flame", verts=6, emit=emit)


def crack(name, pts, r, colour, emit=1.3):
    """A glowing line along a run of points: a crack with fire in it."""
    for k, (p, q) in enumerate(zip(pts, pts[1:])):
        rod("%s_%d" % (name, k), p, q, r, colour, verts=5, emit=emit)
        if k:
            sphere("%s_j_%d" % (name, k), r, p, colour, emit=emit)


def on_cone(a, z, r1, r2, h, out=0.012):
    """A point on the side of a cone standing on the ground, at angle a and
    height z, a hair proud of it."""
    rr = r1 + (r2 - r1) * z / h + out
    return (math.cos(a) * rr, math.sin(a) * rr, z)


def mesh_object(name, verts, faces, colour, rough=0.8, emit=0.0, metal=0.0):
    """A mesh from a list of points and faces, normals put right, flat shaded."""
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], faces)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    me.materials.append(bp.material(name, colour, rough, metal, emit))
    return ob


def pyramid(name, base, r, h, n, colour, lean=(0.0, 0.0), face=-math.pi / 2, rough=0.8, emit=0.0,
            squash=1.0, rng=None, wobble=0.0, alt=None):
    """An n-sided spike standing on `base`, its point pushed over by `lean`
    (as a fraction of its height). One face looks toward `face` (an angle in
    the ground plane; -pi/2 is toward the camera), so something can be set on
    that face: see face_point. `alt` colours every other face, for glass
    whose facets catch the light by turns. Returns the object, its corners
    and its point."""
    bx, by, bz = base
    a0 = face - math.pi / n
    corners = []
    for i in range(n):
        a = a0 + i * math.tau / n
        rr = r * (1.0 + (rng.uniform(-wobble, wobble) if rng else 0.0))
        corners.append((bx + math.cos(a) * rr, by + math.sin(a) * rr * squash, bz))
    apex = (bx + lean[0] * h, by + lean[1] * h, bz + h)
    faces = [(i, (i + 1) % n, n) for i in range(n)] + [tuple(range(n - 1, -1, -1))]
    ob = mesh_object(name, corners + [apex], faces, colour, rough=rough, emit=emit)
    if alt:
        ob.data.materials.append(bp.material(name + "_alt", alt, rough, 0.0, emit))
        for p in ob.data.polygons:
            if p.index < n and p.index % 2 == 1:
                p.material_index = 1
    return ob, corners, apex


def _clip(poly, a, b, c):
    """The part of a convex polygon where a*x + b*y <= c."""
    out = []
    n = len(poly)
    for i in range(n):
        p, q = poly[i], poly[(i + 1) % n]
        fp, fq = a * p[0] + b * p[1] - c, a * q[0] + b * q[1] - c
        if fp <= 0:
            out.append(p)
        if (fp < 0 < fq) or (fq < 0 < fp):
            t = fp / (fp - fq)
            out.append((p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t))
    return out


def cells(seeds, radius, gap, sides=40):
    """Plates that tile a disc: each seed's share of it (its Voronoi cell,
    clipped to the disc), drawn in by `gap` all round so a crack shows
    between every two. Lava crust, crazed earth, the floor of a chasm.
    Returns (seed, outline) pairs, so a caller can leave some out."""
    rim = [(math.cos(k / sides * math.tau) * radius, math.sin(k / sides * math.tau) * radius) for k in range(sides)]
    out = []
    for i, s in enumerate(seeds):
        poly = list(rim)
        for j, t in enumerate(seeds):
            if i == j or math.hypot(t[0] - s[0], t[1] - s[1]) > radius:
                continue
            a, b = t[0] - s[0], t[1] - s[1]
            c = (t[0] ** 2 + t[1] ** 2 - s[0] ** 2 - s[1] ** 2) / 2.0
            poly = _clip(poly, a, b, c)
            if len(poly) < 3:
                break
        if len(poly) < 3:
            continue
        cx = sum(p[0] for p in poly) / len(poly)
        cy = sum(p[1] for p in poly) / len(poly)
        shrunk = []
        for p in poly:
            dx, dy = p[0] - cx, p[1] - cy
            d = math.hypot(dx, dy)
            f = max(0.0, (d - gap) / d) if d > 1e-6 else 0.0
            shrunk.append((cx + dx * f, cy + dy * f))
        out.append((s, shrunk))
    return out


def plate(name, poly, z0, z1, colour, emit=0.0, rough=0.9):
    """A slab with the outline of `poly`, from z0 up to z1."""
    n = len(poly)
    verts = [(p[0], p[1], z0) for p in poly] + [(p[0], p[1], z1) for p in poly]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    return mesh_object(name, verts, faces, colour, rough=rough, emit=emit)


def scatter_seeds(rng, radius, step, inner=0.0):
    """Points on a jittered grid inside a ring: seeds for `cells`."""
    pts = []
    k = int(radius / step) + 2
    for j in range(-k, k + 1):
        for i in range(-k, k + 1):
            x = (i + 0.5 * (j % 2)) * step + rng.uniform(-0.3, 0.3) * step
            y = j * step * 0.866 + rng.uniform(-0.3, 0.3) * step
            if inner <= math.hypot(x, y) <= radius + step * 0.4:
                pts.append((x, y))
    return pts


def _axes(base, tip, spin=0.0):
    """Unit vectors along base->tip and two across it, and the length."""
    w = Vector(tip) - Vector(base)
    length = w.length
    w.normalize()
    ref = Vector((0.0, 0.0, 1.0)) if abs(w.z) < 0.95 else Vector((1.0, 0.0, 0.0))
    u = w.cross(ref).normalized()
    v = w.cross(u).normalized()
    cs, sn = math.cos(spin), math.sin(spin)
    return w, u * cs + v * sn, v * cs - u * sn, length


def crystal(name, base, tip, r, colour, alt=None, n=6, point=0.3, emit=0.3, rough=0.3, spin=0.0):
    """A crystal: an n-sided prism from `base`, and a point on it ending at
    `tip`. `alt` colours every other facet, which is most of what makes a
    crystal read as cut rather than as a pencil."""
    w, u, v, length = _axes(base, tip, spin)
    b = Vector(base)
    sh = b + w * length * (1.0 - point)
    ring = [u * math.cos(i / n * math.tau) * r + v * math.sin(i / n * math.tau) * r for i in range(n)]
    verts = [b + o for o in ring] + [sh + o for o in ring] + [Vector(tip)]
    faces = [tuple(range(n - 1, -1, -1))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    if point > 0.0:
        faces += [(n + i, n + (i + 1) % n, 2 * n) for i in range(n)]
    else:
        faces.append(tuple(range(n, 2 * n)))          # a flat end: a length of prism
    ob = mesh_object(name, verts, faces, colour, rough=rough, emit=emit)
    if alt:
        ob.data.materials.append(bp.material(name + "_alt", alt, rough, 0.0, emit))
        for p in ob.data.polygons:
            if p.index >= 1 and ((p.index - 1) % n) % 2 == 1:
                p.material_index = 1
    return ob


def standing_slab(name, w, t, h, colour, rng, taper=0.86, slant=0.22, rough=0.9):
    """A standing stone at the origin, its broad face toward the camera (-Y):
    a slab narrowing a little to a top broken off at a slant, every corner
    knocked a little out of true. Returns the object and a function giving
    the point on its front face at (x, z), for setting runes into it."""
    j = lambda: rng.uniform(-0.025, 0.025)
    hw, ht = w / 2, t / 2
    tw, tt = hw * taper, ht * taper
    # The front face is kept flat (only its corners' x and z move), so what is
    # set into it lies on it.
    verts = [(-hw + j(), -ht, 0), (hw + j(), -ht, 0), (hw + j(), ht, 0), (-hw + j(), ht, 0),
             (-tw + j(), -tt, h + j()), (tw + j(), -tt, h - slant + j()),
             (tw + j(), tt + j(), h - slant * 0.7 + j()), (-tw + j(), tt + j(), h + 0.05 + j())]
    faces = [(3, 2, 1, 0), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    ob = mesh_object(name, verts, faces, colour, rough=rough)
    bp.bevel(ob, 0.03)

    def front(x, z, out=0.014):
        return (x, -ht + (ht - tt) * (z / h) - out, z)
    return ob, front


def annulus(name, r_in, r_out, z0, z1, colour, emit=0.0, sides=36, rough=0.9):
    """A flat ring: a kerb, a lip, the floor round a hole."""
    ci = [(math.cos(k / sides * math.tau), math.sin(k / sides * math.tau)) for k in range(sides)]
    verts = [(c * r_in, s * r_in, z0) for c, s in ci] + [(c * r_out, s * r_out, z0) for c, s in ci]
    verts += [(c * r_in, s * r_in, z1) for c, s in ci] + [(c * r_out, s * r_out, z1) for c, s in ci]
    n = sides
    faces = []
    for k in range(n):
        k2 = (k + 1) % n
        faces.append((2 * n + k, 3 * n + k, 3 * n + k2, 2 * n + k2))      # top
        faces.append((k, k2, n + k2, n + k))                              # bottom
        faces.append((n + k, n + k2, 3 * n + k2, 3 * n + k))              # outer wall
        faces.append((k, 2 * n + k, 2 * n + k2, k2))                      # inner wall
    return mesh_object(name, verts, faces, colour, rough=rough, emit=emit)


def tube(name, r, z0, z1, colour, emit=0.0, verts=28):
    """A cylinder with no ends: the wall of a shaft, seen from inside."""
    bpy.ops.mesh.primitive_cylinder_add(radius=r, depth=z1 - z0, location=(0, 0, (z0 + z1) / 2), vertices=verts,
                                        end_fill_type="NOTHING")
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(bp.material(name, colour, 0.9, 0.0, emit))
    return ob


def clip_side(ob, axis, value, keep="above"):
    """Cuts a mesh along a plane square to an axis, in world space."""
    bpy.context.view_layer.update()
    mw = ob.matrix_world
    i = "xyz".index(axis)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    if keep == "above":
        doomed = [v for v in bm.verts if (mw @ v.co)[i] < value - 1e-4]
    else:
        doomed = [v for v in bm.verts if (mw @ v.co)[i] > value + 1e-4]
    if doomed:
        bmesh.ops.delete(bm, geom=doomed, context="VERTS")
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def group(before, rot=(0, 0, 0), loc=(0, 0, 0)):
    """Parents everything built since `before` (a set of objects) to an empty,
    and turns and moves that: a thing modelled square-on at the origin, then
    set where it stands."""
    pivot = bpy.data.objects.new("group", None)
    bpy.context.collection.objects.link(pivot)
    made = []
    for ob in list(bpy.context.scene.objects):
        if ob in before or ob is pivot or ob.parent is not None or ob.type not in {"MESH", "EMPTY"}:
            continue
        ob.parent = pivot
        made.append(ob)
    pivot.rotation_euler = rot
    pivot.location = loc
    bpy.context.view_layer.update()
    return pivot


# Angular marks, drawn as strokes in a unit square (x across, z up): runes
# that are not any alphabet's, so they read as old and as nobody's.
GLYPHS = {
    "eye":   ((-0.4, 0.0, 0.0, 0.42), (0.0, 0.42, 0.4, 0.0), (0.4, 0.0, 0.0, -0.42), (0.0, -0.42, -0.4, 0.0)),
    "fork":  ((0.0, -0.5, 0.0, 0.05), (0.0, 0.05, -0.36, 0.5), (0.0, 0.05, 0.36, 0.5)),
    "step":  ((-0.34, -0.5, -0.34, 0.5), (-0.34, 0.0, 0.34, 0.0), (0.34, 0.0, 0.34, -0.5)),
    "arrow": ((0.0, -0.5, 0.0, 0.5), (0.0, 0.5, -0.34, 0.16), (0.0, 0.5, 0.34, 0.16)),
    "zig":   ((-0.32, 0.5, 0.32, 0.17), (0.32, 0.17, -0.32, -0.17), (-0.32, -0.17, 0.32, -0.5)),
    "tri":   ((-0.4, -0.38, 0.0, 0.45), (0.0, 0.45, 0.4, -0.38), (0.4, -0.38, -0.4, -0.38)),
    "hook":  ((-0.2, -0.5, -0.2, 0.5), (-0.2, 0.5, 0.3, 0.2), (-0.2, 0.0, 0.3, -0.3)),
}


def rune(name, at, kind, s, colour, emit=1.3, r=0.03):
    """One mark from GLYPHS on a face: `at` maps (x, z) on the face to a
    point in the world (see standing_slab), so the strokes follow it."""
    for k, (x0, z0, x1, z1) in enumerate(GLYPHS[kind]):
        rod("%s_%d" % (name, k), at(x0 * s, z0 * s), at(x1 * s, z1 * s), r, colour, verts=5, emit=emit)


def face_point(corners, apex, t, s=0.5, out=0.012):
    """A point on a pyramid's first face (the one turned toward `face`): `t`
    of the way from its foot to its point, `s` of the way across it."""
    a, b = corners[0], corners[1]
    foot = _lerp(a, b, s)
    p = _lerp(foot, apex, t)
    # Outward: away from the spike's own axis, in the ground plane.
    cx = sum(c[0] for c in corners) / len(corners)
    cy = sum(c[1] for c in corners) / len(corners)
    ax = cx + (apex[0] - cx) * t
    ay = cy + (apex[1] - cy) * t
    dx, dy = p[0] - ax, p[1] - ay
    d = math.hypot(dx, dy) or 1.0
    return (p[0] + dx / d * out, p[1] + dy / d * out, p[2])


# --- framing ------------------------------------------------------------------
def _centre_in_frame(span, elevation):
    """Moves everything built so its silhouette sits in the middle of the
    camera's frame from top to bottom. The camera looks at a point a third of
    the way up a span-wide cube; a wellspring seen from high up, or anything
    deep, otherwise loses its front edge out of the bottom of the frame while
    the top of the frame stands empty. make_props.ps1 sits the art on the
    bottom of its image afterwards, so where it is framed costs nothing.
    Left and right are not touched: the middle of the image is the anchor."""
    bpy.context.view_layer.update()
    e = math.radians(elevation)
    se, ce = math.sin(e), math.cos(e)
    lo, hi, wide = 1e9, -1e9, 0.0
    for ob in bpy.context.scene.objects:
        if ob.type != "MESH":
            continue
        mw = ob.matrix_world
        for v in ob.data.vertices:
            p = mw @ v.co
            u = p.y * se + p.z * ce
            lo, hi = min(lo, u), max(hi, u)
            wide = max(wide, abs(p.x))
    if lo > hi:
        return
    if hi - lo > span * 0.97 or wide > span * 0.485:
        print("primordium: framing is tight: %.2f tall, %.2f wide in a %.2f frame" % (hi - lo, wide * 2, span))
    dz = (0.34 * span * ce - (lo + hi) / 2) / ce
    pivot = bpy.data.objects.new("frame", None)
    bpy.context.collection.objects.link(pivot)
    for ob in list(bpy.context.scene.objects):
        if ob is pivot or ob.parent is not None or ob.type not in {"MESH", "EMPTY"}:
            continue
        ob.parent = pivot
    pivot.location = (0.0, 0.0, dz)


def _without_denoiser():
    """Renders the next image without the denoiser, then puts things back.
    The denoiser wants its memory all at once, and at 2048px on a machine
    with other work open it asked for more than there was: OIDN failed "out
    of memory" and Cycles saved the dais as an opaque black square. Nothing
    is lost by doing without it: every final pixel is the average of an 8x8
    block of rendered ones, and that averages the noise away. setup_render
    turns the denoiser back on for whatever is rendered next anyway; the
    handler takes itself off after the one render all the same."""
    def pre(scene, *_):
        scene.cycles.use_denoising = False
        if pre in bpy.app.handlers.render_pre:
            bpy.app.handlers.render_pre.remove(pre)
    bpy.app.handlers.render_pre.append(pre)


def _framed(builder):
    """A builder, then centred in its frame (see _centre_in_frame)."""
    def build():
        framed = builder()
        span, elevation = framed if isinstance(framed, tuple) else (framed, bp.CAMERA_ELEVATION)
        _centre_in_frame(span, elevation)
        return framed
    build.__name__ = builder.__name__
    build.__doc__ = builder.__doc__
    return build


# =================================================================================
#  The Kiln (fire)
# =================================================================================

def prop_kiln_vent():
    """A cone of black basalt with its top open, fire spitting out of the
    mouth, and the sides split by cracks that glow with the same fire inside.
    Sparks over it, cinders round its foot. Not the plateau's steam vent:
    that one is grey and breathes white; this one is black and burns."""
    rng = random.Random(101)
    R1, R2, H = 0.62, 0.19, 0.92
    ob = frustum("cone", R1, R2, H, (0, 0, H / 2), "pm_basalt", verts=10)
    jitter(ob, rng, 0.035)
    flat(ob)
    # A lip round the mouth, and the mouth full of light.
    torus("lip", R2 + 0.02, 0.065, (0, 0, H - 0.01), "pm_basalt_lt", scale=(1.0, 1.0, 0.8))
    cyl("throat", R2, 0.05, (0, 0, H - 0.01), "pm_magma_hot", verts=12, emit=1.4)
    # Cracks down the face toward the camera, from the mouth to near the foot.
    for k, (a0, a1, z1) in enumerate(((-1.95, -2.3, 0.1), (-1.3, -1.05, 0.16), (-0.6, -0.35, 0.3))):
        pts = []
        n = 5
        for j in range(n + 1):
            t = j / n
            z = H - 0.06 - (H - 0.06 - z1) * t
            a = a0 + (a1 - a0) * t + (0.1 if j % 2 else -0.1) * (1 if k % 2 else -1) * (0.0 if j in (0, n) else 1.0)
            pts.append(on_cone(a, z, R1, R2, H))
        crack("crack_%d" % k, pts, 0.032, "pm_magma", emit=1.3)
    # The fire, a tall one spat up out of the mouth. No loose sparks: a spark
    # is a pixel, and the outline pass turns a lone pixel dark.
    fire("fire", 0.0, 0.0, H - 0.04, 0.24, 0.82, emit=1.2, lean=(0.05, 0.0), seed=3)
    # Cinders and black rocks round its foot, one still alight.
    for k, (a, d, s) in enumerate(((-2.6, 0.68, 0.14), (-1.7, 0.72, 0.11), (-0.5, 0.7, 0.13), (0.5, 0.68, 0.1),
                                   (2.5, 0.66, 0.12))):
        bp.rock("cinder_%d" % k, (s * 1.2, s, s * 0.8), (math.cos(a) * d, math.sin(a) * d, s * 0.4),
                "pm_basalt_dk" if k % 2 else "pm_basalt", rng)
    bp.rock("hot_cinder", (0.08, 0.07, 0.06), (-0.5, -0.5, 0.04), "pm_magma_dk", rng)
    bpy.context.active_object.data.materials[0] = bp.material("hot_cinder_lit", "pm_magma_dk", 0.9, 0.0, 1.2)
    mound("ash", 0.5, (0, 0.02, 0), "pm_ash", (1.5, 1.35, 0.14))
    return 2.0


def prop_obsidian_spire():
    """A tall spike of black volcanic glass, five-sided and sharp-edged, with
    two lesser ones grown off its foot and splinters round them. Glassy: the
    faces are smooth enough to catch the light, so one goes pale and the
    rest stay black. In the face toward you, a seam of orange fire, forked,
    as if the glass had set round it."""
    rng = random.Random(102)
    main, corners, apex = pyramid("spire", (0.0, 0.05, 0.0), 0.46, 2.6, 5, "pm_obsidian", lean=(0.05, 0.02),
                                  rough=0.2, rng=rng, wobble=0.08, alt="pm_obsidian_lt")
    pyramid("spire_b", (0.42, 0.24, 0.0), 0.27, 1.5, 5, "pm_obsidian_lt", lean=(0.2, 0.06), face=-1.2, rough=0.22,
            alt="pm_obsidian_hi")
    pyramid("spire_c", (-0.38, -0.04, 0.0), 0.23, 1.05, 5, "pm_obsidian", lean=(-0.22, -0.04), face=-2.0,
            rough=0.2, alt="pm_obsidian_lt")
    for k, (x, y, r, h, lx, ly) in enumerate(((0.3, -0.32, 0.12, 0.5, 0.25, -0.3), (-0.55, -0.3, 0.1, 0.36, -0.4, -0.2),
                                              (0.66, -0.06, 0.1, 0.42, 0.45, -0.1), (-0.12, -0.42, 0.08, 0.26, 0.1, -0.5),
                                              (-0.62, 0.22, 0.11, 0.48, -0.35, 0.1))):
        pyramid("splinter_%d" % k, (x, y, 0.0), r, h, 4, "pm_obsidian_lt" if k % 2 else "pm_obsidian",
                lean=(lx, ly), face=rng.uniform(-2.5, -0.6), rough=0.25, alt="pm_obsidian_hi" if k % 2 else None)
    # The seam: up the face toward the camera, a fork near its top.
    pts = [face_point(corners, apex, t, s) for t, s in ((0.1, 0.52), (0.2, 0.44), (0.31, 0.55), (0.42, 0.47))]
    crack("seam", pts, 0.034, "pm_magma", emit=1.3)
    crack("seam_fork", [pts[-1], face_point(corners, apex, 0.52, 0.36)], 0.028, "pm_magma", emit=1.3)
    crack("seam_fork_b", [pts[-1], face_point(corners, apex, 0.55, 0.6)], 0.026, "pm_magma", emit=1.3)
    # Black grit and rubble round the foot.
    for k in range(7):
        a = k / 7 * math.tau + rng.uniform(-0.3, 0.3)
        d = rng.uniform(0.58, 0.72)
        s = rng.uniform(0.07, 0.11)
        bp.rock("grit_%d" % k, (s * 1.3, s, s * 0.7), (math.cos(a) * d, math.sin(a) * d * 0.8, s * 0.3),
                "pm_basalt" if k % 2 else "pm_basalt_dk", rng)
    mound("foot", 0.5, (0, 0.04, 0), "pm_ash", (1.45, 1.1, 0.12))
    return 3.0


def prop_cinder_heap():
    """A low heap of cinders, black and dark red, with live ones among them
    and the heap's heart still glowing through the gaps."""
    rng = random.Random(104)
    RX, RY, H = 0.56, 0.44, 0.34
    mound("bed", 1.0, (0, 0.0, 0), "pm_ash", (RX, RY, H * 0.8))
    # The heart of it, glowing, showing only between the cinders on top.
    mound("heart", 1.0, (0, 0.02, 0), "pm_magma_dk", (RX * 0.62, RY * 0.62, H))
    bpy.context.active_object.data.materials[0] = bp.material("heart_lit", "pm_magma_dk", 0.9, 0.0, 1.0)
    k = 0
    for ring, (d, n) in enumerate(((0.0, 1), (0.34, 7), (0.66, 10), (0.92, 13))):
        for j in range(n):
            a = (j + 0.5 * ring) / n * math.tau + rng.uniform(-0.2, 0.2)
            x, y = math.cos(a) * d * RX, math.sin(a) * d * RY
            z = H * 0.8 * math.sqrt(max(0.0, 1.0 - d * d)) + 0.02
            s = rng.uniform(0.075, 0.105) * (1.0 - 0.25 * d)
            live = (k % 3 == 1) and d < 0.95
            col = ("pm_magma" if k % 6 == 1 else "pm_magma_dk") if live else ("pm_basalt", "pm_crust", "pm_basalt_dk")[k % 3]
            ob = bp.rock("cinder_%d" % k, (s * 1.25, s, s * 0.85), (x, y, z), col, rng)
            if live:
                ob.data.materials[0] = bp.material("cinder_lit_%d" % k, col, 0.9, 0.0, 1.15)
            k += 1
    return 1.5


def prop_magma_well():
    """The Kiln's wellspring: a ring of black boulders, tall at the back and
    low at the front so the pool shows, round a pool of magma -- orange, gold
    at the middle, plates of dark crust riding on it and bubbles rising --
    and out of the middle of it a column of flame standing taller than a man.
    Seen from higher than the furniture: it is the pool that matters."""
    rng = random.Random(103)
    RP = 1.42
    cyl("bed", RP + 0.14, 0.08, (0, 0, 0.03), "pm_basalt_dk", verts=28)
    cyl("pool", RP, 0.06, (0, 0, 0.07), "pm_magma", verts=28, emit=1.1)
    cyl("pool_hot", 0.62, 0.06, (0.0, 0.0, 0.075), "pm_magma_hot", verts=20, emit=1.2)
    # Crust: dark plates riding on the magma, crazed into pieces with the light
    # showing in every crack -- a pool of bright orange with nothing on it is
    # only a disc. Clear round the column, where it is too hot to skin over.
    for k, (seed, poly) in enumerate(cells(scatter_seeds(rng, RP, 0.36), RP - 0.05, 0.035)):
        if math.hypot(*seed) < 0.74:
            continue
        plate("crust_%d" % k, poly, 0.09, 0.115 + rng.uniform(0.0, 0.01), "pm_crust" if k % 4 else "pm_basalt_dk",
              emit=0.3 if k % 4 else 0.0)
    for k, (x, y, r) in enumerate(((0.5, -0.36, 0.08), (-0.56, -0.2, 0.07), (0.3, 0.5, 0.07))):
        clip_below(sphere("bubble_%d" % k, r, (x, y, 0.1), "pm_magma_hot", emit=1.3), 0.1)
    # The ring of boulders: low in front, high behind, big and small and not
    # evenly spaced, shouldered into one another so the ring is a wall of rock
    # and not a cog. A few are split, the fire showing in the split.
    a = 0.0
    k = 0
    while a < math.tau - 0.2:
        back = 0.5 + 0.5 * math.sin(a)             # 0 at the front, 1 at the back
        sz = (0.3 + 0.12 * back) * rng.uniform(0.75, 1.25)
        d = RP + 0.3 + sz * 0.25 + rng.uniform(-0.05, 0.05)
        h = (0.18 + 0.38 * back) * rng.uniform(0.75, 1.2)
        x, y = math.cos(a) * d, math.sin(a) * d
        ob = bp.rock("boulder_%d" % k, (sz, sz * 0.85, h), (x, y, h * 0.45), ("pm_basalt", "pm_basalt_dk",
                     "pm_basalt_lt")[k % 3], rng)
        ob.rotation_euler = (rng.uniform(-0.12, 0.12), rng.uniform(-0.12, 0.12), a + rng.uniform(-0.4, 0.4))
        clip_below(ob)
        if k in (2, 7, 12):
            p = (x - math.cos(a) * sz * 0.6, y - math.sin(a) * sz * 0.6, h * 0.8)
            crack("split_%d" % k, [p, (p[0], p[1], h * 0.4), (p[0] + 0.05, p[1], 0.08)], 0.03, "pm_magma", emit=1.3)
        a += (sz * 1.35) / d
        k += 1
    # Stones round the pool's edge, so the rim is solid and the pool has a lip.
    for k in range(26):
        a = k / 26 * math.tau
        bp.rock("rim_%d" % k, (0.14, 0.11, 0.08), (math.cos(a) * (RP + 0.08), math.sin(a) * (RP + 0.08), 0.07),
                "pm_basalt_dk" if k % 2 else "pm_basalt", rng)
    column_of_fire("column", 0.0, 0.0, 0.08, 0.5, 3.4)
    for k, (x, y, h) in enumerate(((0.95, -0.36, 0.5), (-1.0, 0.08, 0.56), (0.3, 1.0, 0.42))):
        fire("lick_%d" % k, x, y, 0.08, 0.13, h, emit=1.15, tongues=3, seed=20 + k)
    return (5.0, WELL)


# =================================================================================
#  The Bedrock (earth)
# =================================================================================

def prop_crystal_cluster():
    """Crystals grown out of a shelf of grey-brown rock: a tall pale green
    one in the middle, amber ones and lesser green ones splayed round it,
    every one six-sided and pointed, its facets light and dark by turns, and
    lit a little from inside."""
    rng = random.Random(201)
    S = 1.15                       # the whole cluster, a size up from first drawn

    def at(p):
        return (p[0] * S, p[1] * S, p[2] * S)
    for k, (x, y, sx, sy, sz, col) in enumerate(((0.0, 0.08, 0.5, 0.42, 0.3, "pm_earth"),
                                                 (-0.42, 0.0, 0.34, 0.3, 0.2, "pm_earth_dk"),
                                                 (0.44, -0.02, 0.36, 0.3, 0.22, "pm_earth_lt"),
                                                 (0.1, -0.3, 0.28, 0.2, 0.13, "pm_earth"))):
        clip_below(bp.rock("shelf_%d" % k, (sx * S, sy * S, sz * S), at((x, y, sz * 0.4)), col, rng))
    G, A = ("pm_jade", "pm_jade_dk"), ("pm_topaz", "pm_topaz_dk")
    for k, (base, tip, r, (col, alt)) in enumerate((
            ((0.02, 0.06, 0.2), (0.06, -0.02, 1.62), 0.17, G),
            ((-0.2, 0.04, 0.16), (-0.66, -0.08, 1.06), 0.13, A),
            ((0.22, 0.06, 0.16), (0.64, -0.06, 1.18), 0.13, G),
            ((0.08, 0.26, 0.2), (0.3, 0.4, 0.98), 0.11, A),
            ((-0.1, 0.24, 0.2), (-0.34, 0.4, 0.82), 0.09, G),
            ((0.14, -0.16, 0.14), (0.36, -0.44, 0.6), 0.09, A),
            ((-0.16, -0.16, 0.12), (-0.4, -0.46, 0.5), 0.08, G),
            ((0.46, -0.1, 0.12), (0.74, -0.2, 0.46), 0.07, G))):
        crystal("crystal_%d" % k, at(base), at(tip), r * S, col, alt=alt, emit=0.35, spin=rng.uniform(0, 1))
    # A loose one lying at the foot, and grit.
    crystal("loose", at((-0.56, -0.36, 0.05)), at((-0.3, -0.5, 0.07)), 0.06 * S, "pm_topaz", alt="pm_topaz_dk",
            emit=0.35)
    for k in range(5):
        a = k * 1.3 + 0.5
        bp.rock("grit_%d" % k, (0.08, 0.07, 0.05), at((math.cos(a) * 0.7, math.sin(a) * 0.45 - 0.05, 0.03)),
                "pm_earth_dk" if k % 2 else "pm_earth", rng)
    return 2.25


def prop_monolith():
    """A standing slab of dark stone twice a man's height, its top broken off
    at a slant, and down its face a column of runes joined by veins, all of
    it burning amber -- as if the light were in the rock and the runes were
    where it got out. Rubble at its foot, moss in the lee of it, and a few
    pale green crystals come up through the rubble."""
    rng = random.Random(202)
    W, T, H = 1.06, 0.48, 2.85
    ob, front = standing_slab("slab", W, T, H, "pm_slab", rng, taper=0.84, slant=0.34)
    # Chips knocked off its edges show paler stone.
    for k, (x, z) in enumerate(((-0.5, 0.62), (0.47, 1.5), (-0.44, 2.2))):
        blk("chip_%d" % k, (0.1, 0.05, 0.16), (x, front(x, z)[1] + 0.01, z), "pm_slab_lt", rot=(0, 0.4, 0), bev=0.01)
    # The runes, in a column down the middle; veins run out of them to the
    # edges the way cracks run, forking. (Joined rune to rune down the middle
    # they made a little stick man.) Emission held under 1: amber, not yellow.
    col, e = "pm_amber", 0.95
    marks = ((2.18, "tri"), (1.62, "zig"), (1.06, "step"), (0.52, "hook"))
    for k, (z, kind) in enumerate(marks):
        rune("rune_%d" % k, lambda x, zz, z=z: front(x * 0.84, z + zz), kind, 0.34, col, emit=e, r=0.034)
    for k, (z, side, dz) in enumerate(((2.18, -1, 0.3), (1.62, 1, -0.24), (1.06, -1, -0.3), (0.52, 1, -0.22))):
        x0 = side * 0.19
        pts = [front(x0, z), front(side * 0.28, z + dz * 0.4), front(side * 0.34, z + dz * 0.75),
               front(side * 0.43, z + dz)]
        crack("branch_%d" % k, pts, 0.022, col, emit=e)
        crack("twig_%d" % k, [pts[1], front(side * 0.36, z + dz * 0.1 - 0.1 * side)], 0.02, col, emit=e)
    # The foot: rubble, moss on the lee side, three crystals.
    for k in range(9):
        a = math.pi + k / 8 * math.pi + rng.uniform(-0.15, 0.15)
        x, y = math.cos(a) * 0.68, math.sin(a) * 0.42
        s = rng.uniform(0.09, 0.15)
        bp.rock("rubble_%d" % k, (s * 1.2, s, s * 0.7), (x, y, s * 0.3), ("pm_earth", "pm_earth_dk", "pm_slab_lt")[k % 3],
                rng)
    mound("moss", 0.3, (0.42, 0.18, 0), "pm_moss", (1.2, 0.8, 0.4))
    mound("moss_b", 0.18, (0.6, -0.05, 0), "pm_moss_dk", (1.2, 0.9, 0.5))
    for k, (base, tip, r) in enumerate((((-0.5, -0.28, 0.02), (-0.66, -0.42, 0.42), 0.07),
                                        ((-0.42, -0.32, 0.02), (-0.36, -0.52, 0.3), 0.055),
                                        ((-0.6, -0.2, 0.02), (-0.84, -0.26, 0.26), 0.05))):
        crystal("foot_crystal_%d" % k, base, tip, r, "pm_jade", alt="pm_jade_dk", emit=0.35)
    mound("foot", 0.5, (0, 0.0, 0), "pm_earth_dk", (1.5, 0.95, 0.1))
    return 3.5


def _ring_y(name, inner, outer, y, colour, z0=0.0):
    """A flat ring standing in the plane y = const, facing -Y, between two
    outlines given as radii round the circle (one per side), so its edges
    can be as ragged as broken stone."""
    sides = len(inner)
    verts = [(math.cos(k / sides * math.tau) * inner[k], y, z0 + math.sin(k / sides * math.tau) * inner[k])
             for k in range(sides)]
    verts += [(math.cos(k / sides * math.tau) * outer[k], y, z0 + math.sin(k / sides * math.tau) * outer[k])
              for k in range(sides)]
    faces = [(k, (k + 1) % sides, sides + (k + 1) % sides, sides + k) for k in range(sides)]
    return mesh_object(name, verts, faces, colour, rough=0.9)


def _hollow(prefix, c, RI, rng, keep, count=((0.0, 1), (0.45, 6), (0.85, 9), (1.15, 12)), size=1.0, z0=0.0):
    """The hollow of a geode about (0, c, z0), cut by the plane y = 0: its
    dark violet lining, and crystal points all over it, pointing out of the
    opening. `keep` says which side of the cut this piece is."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=RI, location=(0, c, z0), segments=20, ring_count=10)
    bowl = bpy.context.active_object
    bowl.name = prefix + "_bowl"
    bowl.data.materials.append(bp.material(prefix + "_bowl", "pm_geode_dk", 0.6))
    clip_side(bowl, "y", 0.0, keep=keep)
    sgn = 1.0 if keep == "above" else -1.0
    k = 0
    for ring_i, (el, n) in enumerate(count):
        for j in range(n):
            az = (j + 0.5 * ring_i) / n * math.tau
            # el is the angle from the deepest point of the hollow.
            d = Vector((math.sin(el) * math.cos(az), sgn * math.cos(el), math.sin(el) * math.sin(az)))
            p = Vector((0.0, c, z0)) + d * RI * 0.97
            if sgn * p.y < 0.02:
                continue
            out = (Vector((0.0, -sgn, 0.0)) * 0.7 - d * 0.7).normalized()
            ln = RI * rng.uniform(0.26, 0.38) * size
            light = (k % 3 == 0)
            crystal("%s_pt_%d" % (prefix, k), tuple(p), tuple(p + out * ln), RI * 0.09 * size,
                    "pm_geode_lt" if light else "pm_geode", alt="pm_geode" if light else "pm_geode_dk", n=4,
                    point=0.55, emit=0.45 if light else 0.25, spin=rng.uniform(0, 1))
            k += 1


def prop_geode():
    """A boulder split open. The big piece still stands, rough grey-brown on
    top, its front gone: in the broken face a white band of chalcedony and
    inside it a hollow full of violet crystal. The piece that came away lies
    on its back in front of it, showing its own share of the crystal."""
    rng = random.Random(203)
    R, C = 0.95, 0.3                 # the boulder, and how far behind the break its middle is
    RI, ZH = 0.56, 0.16              # the hollow, and how far above the boulder's middle it sits
    sides = 28
    before = set(bpy.context.scene.objects)
    bpy.ops.mesh.primitive_ico_sphere_add(radius=R, subdivisions=2, location=(0, C, 0))
    shell = bpy.context.active_object
    shell.name = "shell"
    shell.data.materials.append(bp.material("shell", "pm_earth", 0.9))
    for v in shell.data.vertices:
        v.co *= 1.0 + rng.uniform(-0.09, 0.09)
    shell.scale = (1.18, 1.0, 0.86)
    flat(shell)
    clip_side(shell, "y", 0.0, keep="above")
    # The broken face, ragged at both edges -- perfect circles inside circles
    # read as a portal -- and the band round the opening, ragged too.
    r_open = math.sqrt(RI * RI - C * C)
    jag = [1.0 + rng.uniform(-0.1, 0.1) for _ in range(sides)]
    band_in = [r_open * j for j in jag]
    band_out = [r_open * j + 0.07 + rng.uniform(0.0, 0.04) for j in jag]
    # The rim of the shell where the break cuts it is an ellipse 1.18 wide and
    # 0.86 high; the face stops a little inside it, measured from the opening.
    rr = math.sqrt(R * R - C * C)
    shifted = []
    for k in range(sides):
        a = k / sides * math.tau
        rim = rr / math.sqrt((math.cos(a) / 1.18) ** 2 + (math.sin(a) / 0.86) ** 2)
        shifted.append(rim * 0.93 * (1.0 + rng.uniform(-0.05, 0.03)) - ZH * math.sin(a))
    _ring_y("cut", band_out, [max(b + 0.05, s) for b, s in zip(band_out, shifted)], -0.006, "pm_earth_lt", z0=ZH)
    _ring_y("band", band_in, band_out, -0.012, "pm_rind", z0=ZH)
    _hollow("hollow", C, RI, rng, "above", z0=ZH)
    # Leaned back a little, so the opening looks up at you, and sat down.
    group(before, rot=(-0.3, 0.0, 0.14), loc=(-0.3, 0.3, 0.52))
    for ob in bpy.context.scene.objects:
        if ob not in before and ob.type == "MESH":
            clip_below(ob)

    # The piece that broke away, on its back: a shallow bowl of rind with the
    # band and the crystal on its upturned face.
    before = set(bpy.context.scene.objects)
    RC = 0.52
    bpy.ops.mesh.primitive_ico_sphere_add(radius=RC, subdivisions=2, location=(0, 0, 0))
    cap = bpy.context.active_object
    cap.name = "cap"
    cap.data.materials.append(bp.material("cap", "pm_earth", 0.9))
    for v in cap.data.vertices:
        v.co *= 1.0 + rng.uniform(-0.08, 0.08)
    cap.scale = (1.15, 1.0, 0.55)
    flat(cap)
    clip_side(cap, "z", 0.0, keep="below")
    cyl("cap_cut", RC * 0.98, 0.02, (0, 0, 0.0), "pm_earth_lt", verts=24).scale = (1.15, 1.0, 1.0)
    cyl("cap_band", RC * 0.7, 0.024, (0, 0, 0.004), "pm_rind", verts=24).scale = (1.15, 1.0, 1.0)
    cyl("cap_hollow", RC * 0.58, 0.028, (0, 0, 0.006), "pm_geode_dk", verts=24).scale = (1.15, 1.0, 1.0)
    for k in range(14):
        a = k / 14 * math.tau * 2.0 + rng.uniform(-0.2, 0.2)
        d = RC * (0.16 if k < 4 else 0.4) * rng.uniform(0.7, 1.1)
        p = (math.cos(a) * d * 1.15, math.sin(a) * d, 0.01)
        crystal("cap_pt_%d" % k, p, (p[0] * 0.8, p[1] * 0.8, 0.01 + rng.uniform(0.11, 0.18)), 0.05,
                "pm_geode_lt" if k % 3 == 0 else "pm_geode", alt="pm_geode_dk", n=4, point=0.55,
                emit=0.45 if k % 3 == 0 else 0.25, spin=rng.uniform(0, 1))
    # Tipped up toward you so the face shows, its far rim dug into the dirt.
    group(before, rot=(0.42, 0.0, 0.25), loc=(0.66, -0.4, 0.2))
    for ob in bpy.context.scene.objects:
        if ob not in before and ob.type == "MESH":
            clip_below(ob)
    for k, (x, y, a) in enumerate(((0.12, -0.72, 0.4), (-0.42, -0.66, -0.6), (1.12, 0.12, 1.2))):
        crystal("loose_%d" % k, (x, y, 0.03), (x + math.cos(a) * 0.2, y + math.sin(a) * 0.1, 0.1), 0.05,
                "pm_geode_lt" if k % 2 else "pm_geode", alt="pm_geode_dk", n=4, point=0.5, emit=0.35)
    for k in range(6):
        a = k * 1.1 + 0.2
        bp.rock("chip_%d" % k, (0.09, 0.07, 0.05), (math.cos(a) * 1.1, math.sin(a) * 0.58, 0.04),
                "pm_earth" if k % 2 else "pm_earth_dk", rng)
    mound("dirt", 0.5, (0, 0.0, 0), "pm_earth_dk", (2.3, 1.25, 0.08))
    return 3.0


def prop_stone_well():
    """The Bedrock's wellspring: a round floor of grey-brown flagstones, crazed
    into plates, with a chasm in the middle of it going down in courses of
    rock to a floor that burns amber; the light comes up through the cracks
    round its mouth. A ring of small monoliths stands round it, open at the
    front, each with one rune lit on the face it turns to the chasm, and pale
    green crystals have grown at its lip."""
    rng = random.Random(204)
    RH, RA, DEPTH = 1.0, 2.05, 0.95
    # What shows in the cracks: amber light near the chasm, dark further out.
    # The Bedrock's light is kept under an emission of 1 everywhere: higher,
    # amber blows out to the Kiln's yellow.
    annulus("glow_bed", RH - 0.04, 1.42, 0.0, 0.06, "pm_amber", emit=0.6)
    annulus("bed", 1.42, RA, 0.0, 0.06, "pm_earth_dk")
    for k, (seed, poly) in enumerate(cells(scatter_seeds(rng, RA, 0.42), RA, 0.04)):
        if math.hypot(*seed) < RH + 0.16:
            continue
        plate("flag_%d" % k, poly, 0.06, 0.12 + rng.uniform(0.0, 0.03), ("pm_earth", "pm_earth_lt", "pm_earth_dk")[k % 3])
    # The chasm: its wall in courses going down, and the floor alight.
    tube("wall_a", RH, -0.32, 0.07, "pm_earth_dk")
    tube("wall_b", RH * 0.97, -0.64, -0.32, "pm_earth")
    tube("wall_c", RH * 0.94, -DEPTH, -0.64, "pm_earth_dk")
    cyl("floor", RH * 0.94, 0.04, (0, 0, -DEPTH), "pm_amber", verts=28, emit=0.8)
    cyl("floor_hot", RH * 0.5, 0.05, (0, 0.18, -DEPTH + 0.01), "pm_amber_hot", verts=20, emit=0.9)
    for k, (a, h) in enumerate(((1.2, 0.5), (1.9, 0.38), (2.5, 0.44), (0.6, 0.34))):
        x, y = math.cos(a) * RH * 0.7, math.sin(a) * RH * 0.7
        crystal("deep_%d" % k, (x, y, -DEPTH), (x * 1.15, y * 1.1, -DEPTH + h), 0.08, "pm_amber_hot",
                alt="pm_amber", emit=0.8)
    # The ring of monoliths, open at the front.
    n = 7
    for k in range(n):
        # From front-right round the back to front-left: the front is left open.
        a = math.radians(-90 + 34) + k * math.radians(360 - 68) / (n - 1)
        x, y = math.cos(a) * 1.66, math.sin(a) * 1.66
        before = set(bpy.context.scene.objects)
        h = 1.1 + 0.32 * (0.5 + 0.5 * math.sin(a)) + rng.uniform(-0.06, 0.06)
        ob, front = standing_slab("stone_%d" % k, 0.44, 0.26, h, ("pm_slab", "pm_slab_dk", "pm_slab_lt")[k % 3], rng,
                                  slant=0.14)
        rune("stone_rune_%d" % k, lambda xx, zz, h=h: front(xx, h * 0.62 + zz), ("eye", "fork", "step", "arrow", "zig",
             "tri", "hook")[k], 0.24, "pm_amber", emit=0.95, r=0.026)
        # Turned so the face with the rune looks in at the chasm.
        group(before, rot=(0, 0, a - math.pi / 2), loc=(x, y, 0.08))
    # Pale green crystals grown at the lip, plenty of them: amber and green
    # together are the Bedrock's; a glowing pit alone is the Kiln's too.
    for k, (a, n_) in enumerate(((3.6, 3), (4.5, 2), (5.5, 3), (0.4, 3), (1.6, 2), (2.5, 3))):
        for j in range(n_):
            aa = a + (j - 1) * 0.13
            x, y = math.cos(aa) * (RH + 0.22), math.sin(aa) * (RH + 0.22)
            crystal("lip_%d_%d" % (k, j), (x, y, 0.08), (x * 1.1 + (j - 1) * 0.1, y * 1.1, 0.08 + 0.4 + 0.12 * (j % 2)),
                    0.08, "pm_jade", alt="pm_jade_dk", emit=0.35)
    return (5.0, WELL)


# =================================================================================
#  The Deeps (water)
# =================================================================================

def coral(prefix, base, rng, depth=3, length=0.62, r=0.12, colour="pm_coral", tip="pm_coral_lt", spread=0.62,
          shrink=0.74):
    """Branching coral grown from `base`: a stem that forks and forks again,
    every branch thinner and shorter, mostly upward and spread side to side
    (toward and away from the camera a branch is only a dot), each ending in
    a rounded knob a shade paler."""
    count = [0]

    def grow(p, d, ln, rr, level, side):
        q = p + d * ln
        i = count[0]
        count[0] += 1
        rod("%s_%d" % (prefix, i), tuple(p), tuple(q), rr, colour, verts=8)
        sphere("%s_j_%d" % (prefix, i), rr * 1.02, tuple(q), colour)
        if level == 0:
            sphere("%s_tip_%d" % (prefix, i), rr * 1.32, tuple(q), tip)
            return
        n = 3 if rng.random() < 0.3 else 2
        for c in range(n):
            s = (-1 if c % 2 == 0 else 1) * (1 if side >= 0 else -1)
            if n == 3 and c == 2:
                s = 0
            ax = Vector((s * spread + rng.uniform(-0.15, 0.15), rng.uniform(-0.25, 0.25), 0.0))
            nd = (d + ax + Vector((0.0, 0.0, 0.25))).normalized()
            grow(q, nd, ln * shrink * rng.uniform(0.9, 1.1), rr * 0.74, level - 1, s or side)
    grow(Vector(base), Vector((0.0, 0.0, 1.0)), length, r, depth, 1)


def frond(prefix, x0, y0, h, w, colour, rng, phase, amp=0.12, freq=2.2, segs=10, lean=0.0, rib="pm_kelp"):
    """One kelp frond standing up from the bed: a long ribbon of a blade,
    narrow at its foot and its tip, swaying side to side as it rises, its
    broad face to the camera, a darker midrib up it."""
    pts = []
    for i in range(segs + 1):
        t = i / segs
        x = x0 + amp * math.sin(t * freq * math.pi + phase) * (0.3 + t) + lean * t * h
        pts.append((x, y0 + 0.03 * math.sin(t * 5 + phase), 0.06 + t * h))
    for i in range(segs):
        p, q = pts[i], pts[i + 1]
        dx, dz = q[0] - p[0], q[2] - p[2]
        t = (i + 0.5) / segs
        width = w * (0.45 + 0.55 * math.sin(min(1.0, t * 1.25) * math.pi * 0.92))
        blk("%s_%d" % (prefix, i), (width, 0.035, math.hypot(dx, dz) * 1.12), _lerp(p, q, 0.5), colour,
            rot=(0, math.atan2(dx, dz), 0), bev=0.008)
        rod("%s_rib_%d" % (prefix, i), (p[0], p[1] - 0.022, p[2]), (q[0], q[1] - 0.022, q[2]), 0.016, rib, verts=5)
    return pts


def seashell(name, turns=3.4, k=0.125, R0=1.0, r0=0.62, H0=1.25, steps=24, around=16, flare=1.28, lip=0.5,
             throat=0.55):
    """A spiral shell: a tube that grows as it coils round an axis and climbs
    it, so the old whorls make a pointed spire and the newest a broad body,
    its mouth flared into a lip. Banded in spirals; the lip and the inside of
    the mouth are pearl, darkening to a pink throat. The apex is at the
    origin and the axis is +Z; the mouth looks along +Y."""
    n_t = int(turns * steps)
    th0 = -turns * math.tau
    verts, faces, mats = [], [], []
    ring_of = []
    for i in range(n_t + 1):
        th = th0 + (i / n_t) * (-th0)
        g = math.exp(k * th)
        f = 1.0 + (flare - 1.0) * max(0.0, (th + 0.9) / 0.9)
        ring_of.append(th)
        for j in range(around):
            s = j / around * math.tau
            rr = R0 + r0 * f * math.cos(s)
            verts.append((g * rr * math.cos(th), g * rr * math.sin(th), g * (H0 + r0 * f * math.sin(s))))
    for i in range(n_t):
        for j in range(around):
            a = i * around + j
            b = i * around + (j + 1) % around
            faces.append((a, b, (i + 1) * around + (j + 1) % around, (i + 1) * around + j))
            if ring_of[i] > -lip:
                mats.append(2)                         # the lip: pearl
            else:
                mats.append(1 if j % 4 == 1 else 0)    # spiral bands
    # The throat: the tube closed a little way in, dark pink, so the mouth
    # has a depth to it behind the pearl.
    it = min(range(n_t + 1), key=lambda i: abs(ring_of[i] + throat))
    faces.append(tuple(it * around + j for j in range(around)))
    mats.append(3)
    ob = mesh_object(name, verts, faces, "pm_shell", rough=0.6)
    for m, col, e in ((1, "pm_shell_band", 0.0), (2, "pm_pearl", 0.15), (3, "pm_pearl_pink", 0.1)):
        ob.data.materials.append(bp.material("%s_%d" % (name, m), col, 0.5, 0.0, e))
    for p, m in zip(ob.data.polygons, mats):
        p.material_index = m
    return ob


def whirlpool(name, R, depth, rings=18, around=80, arms=4, twist=1.9):
    """A whirlpool: water falling in a funnel toward its middle, darker blue
    at the rim, paler as it goes down, and glowing teal at the eye, with
    arms of white foam wound in toward the middle as it turns."""
    verts = [(0.0, 0.0, -depth)]
    for i in range(1, rings + 1):
        r = R * i / rings
        z = -depth * (1.0 - r / R) ** 2
        for j in range(around):
            a = j / around * math.tau
            verts.append((math.cos(a) * r, math.sin(a) * r, z))
    faces, mats = [], []

    def tone(rn, a):
        phase = a - twist * math.log(max(rn, 1e-3))
        # Arms wide enough to stay one band at 160px (thinner, they broke into dashes).
        if 0.16 < rn < 0.96 and ((phase * arms / math.tau) % 1.0) < 0.27 - 0.08 * rn:
            return 4                                    # foam
        return 0 if rn > 0.72 else 1 if rn > 0.46 else 2 if rn > 0.22 else 3
    for j in range(around):
        j2 = (j + 1) % around
        faces.append((0, 1 + j, 1 + j2))
        mats.append(3)
    for i in range(1, rings):
        for j in range(around):
            j2 = (j + 1) % around
            a0, b0 = 1 + (i - 1) * around + j, 1 + (i - 1) * around + j2
            a1, b1 = 1 + i * around + j, 1 + i * around + j2
            faces.append((a0, a1, b1, b0))
            mats.append(tone((i + 0.5) / rings, (j + 0.5) / around * math.tau))
    ob = mesh_object(name, verts, faces, "pm_sea_dk", rough=0.4)
    for m, col, e in ((1, "pm_sea", 0.0), (2, "pm_sea_lt", 0.25), (3, "pm_tide", 1.0), (4, "pm_foam", 0.2)):
        ob.data.materials.append(bp.material("%s_%d" % (name, m), col, 0.4, 0.0, e))
    for p, m in zip(ob.data.polygons, mats):
        p.material_index = m
    return ob


def prop_coral_spire():
    """A tree of rose coral taller than a man, forking and forking again into
    knobbed tips, a lesser teal one grown up beside it, both out of a bed of
    blue-grey stone with a dome of brain coral and a pearl-mouthed shell."""
    rng = random.Random(301)
    coral("rose", (0.0, 0.05, 0.12), rng, depth=3, length=0.8, r=0.14, spread=0.66)
    coral("teal", (0.5, 0.18, 0.1), rng, depth=2, length=0.46, r=0.085, colour="pm_teal", tip="pm_teal_lt",
          spread=0.7)
    coral("teal_b", (-0.48, 0.1, 0.1), rng, depth=2, length=0.36, r=0.075, colour="pm_teal", tip="pm_teal_lt",
          spread=0.7)
    for k, (x, y, sx, sz, col) in enumerate(((0.0, 0.05, 0.42, 0.22, "pm_seastone"), (0.48, 0.12, 0.3, 0.16,
                                              "pm_seastone_dk"), (-0.46, 0.0, 0.32, 0.15, "pm_seastone_lt"))):
        clip_below(bp.rock("bed_%d" % k, (sx, sx * 0.8, sz), (x, y, sz * 0.3), col, rng))
    # Brain coral: a dome ridged round with rings.
    mound("brain", 0.24, (-0.3, -0.28, 0.0), "pm_coral_dk", (1.0, 0.9, 0.75))
    for k in range(3):
        torus("brain_ridge_%d" % k, 0.07 + 0.05 * k, 0.018, (-0.3, -0.28, 0.17 - 0.045 * k), "pm_coral",
              scale=(1.0, 0.9, 1.0))
    sphere("pearl", 0.06, (0.3, -0.36, 0.06), "pm_pearl", emit=0.2)
    mound("sand", 0.5, (0, 0.0, 0), "pm_sand", (1.6, 1.05, 0.08))
    return 3.0


def prop_kelp_stand():
    """A stand of kelp: long ribbon fronds, dark green and olive, rising and
    swaying from a holdfast clamped on a stone, round floats where the
    blades leave the stem."""
    rng = random.Random(302)
    # Few and broad: a blade as narrow as a reed reads as grass.
    for k, (x, y, h, w, col, ph, lean) in enumerate((
            (-0.04, 0.14, 1.95, 0.3, "pm_kelp", 0.4, 0.03),
            (0.3, 0.06, 1.62, 0.28, "pm_kelp_olive", 2.0, 0.1),
            (-0.34, 0.04, 1.5, 0.28, "pm_kelp_olive", 3.4, -0.12),
            (0.1, -0.1, 1.15, 0.26, "pm_kelp_lt", 1.1, 0.03),
            (-0.2, -0.14, 0.9, 0.24, "pm_kelp", 4.4, -0.08))):
        frond("frond_%d" % k, x, y, h, w, col, rng, ph, amp=0.14, lean=lean, segs=12,
              rib="pm_kelp_olive" if col == "pm_kelp" else "pm_kelp")
        sphere("float_%d" % k, 0.07, (x + 0.03, y - 0.06, 0.22 + 0.05 * (k % 3)), "pm_kelp_float")
    # The holdfast: root-like grips over a stone.
    clip_below(bp.rock("stone", (0.42, 0.32, 0.2), (0.0, 0.02, 0.06), "pm_seastone", rng))
    for k in range(9):
        a = k / 9 * math.tau
        rod("grip_%d" % k, (math.cos(a) * 0.1, math.sin(a) * 0.08, 0.2), (math.cos(a) * 0.42, math.sin(a) * 0.3, 0.02),
            0.03, "pm_kelp_olive" if k % 2 else "pm_kelp", verts=5)
    mound("sand", 0.5, (0, 0.0, 0), "pm_sand", (1.4, 0.95, 0.08))
    return 2.25


def prop_giant_shell():
    """A spiral shell bigger than a man lying on its side in the sand: the
    spire to the left, the broad body whorl to the right, cream banded in
    rust, and its mouth turned to you -- a flared pearl lip, and inside it
    the pearly pink of the throat."""
    before = set(bpy.context.scene.objects)
    seashell("shell", lip=0.4)
    # Laid with its spiral to the camera, the whorls winding in on the left,
    # and its mouth open to the right so the pearl shows. Chosen by eye from a
    # dozen trial turns: seen down its spire instead it is a ring.
    piv = group(before, rot=(math.radians(90), 0.0, math.radians(25)))
    piv.scale = (0.68, 0.68, 0.68)
    bpy.context.view_layer.update()
    shell = bpy.data.objects["shell"]
    ws = [shell.matrix_world @ v.co for v in shell.data.vertices]
    minz = min(p.z for p in ws)
    cx = (min(p.x for p in ws) + max(p.x for p in ws)) / 2
    cy = (min(p.y for p in ws) + max(p.y for p in ws)) / 2
    piv.location = (-cx, -cy, -minz - 0.06)
    bpy.context.view_layer.update()
    clip_below(shell)
    rng = random.Random(303)
    mound("sand", 0.5, (0.0, 0.05, 0), "pm_sand", (2.3, 1.2, 0.12))
    for k, (x, y) in enumerate(((-0.9, -0.34), (0.95, -0.4), (0.2, -0.62))):
        bp.rock("pebble_%d" % k, (0.07, 0.06, 0.04), (x, y, 0.04), "pm_seastone_lt" if k % 2 else "pm_seastone", rng)
    return 2.5


def prop_tide_well():
    """The Deeps' wellspring: a whirlpool turning in a ring of stone and coral
    -- blue-grey boulders, rose coral, teal brain coral and a giant clam's
    worth of pearl -- its water falling away in a funnel, arms of foam wound
    into it, and the eye of it glowing teal."""
    rng = random.Random(304)
    RW = 1.45
    whirlpool("whirl", RW, 0.62)
    sphere("eye", 0.16, (0, 0, -0.56), "pm_foam", emit=1.1)
    annulus("rim", RW - 0.04, RW + 0.2, -0.02, 0.06, "pm_seastone_dk")
    # Foam breaking along the edge, in a few long runs rather than a string of beads.
    for k in range(7):
        a0 = k / 7 * math.tau + rng.uniform(0.0, 0.3)
        for j in range(4):
            a = a0 + j * 0.13
            mound("foam_%d_%d" % (k, j), 0.11, (math.cos(a) * (RW - 0.05), math.sin(a) * (RW - 0.05), 0.02),
                  "pm_foam", (1.7, 1.1, 0.55))
    # The ring: a wall of blue-grey boulders shouldered together, coral grown
    # on them and between them, low in front and higher behind.
    a = 0.0
    k = 0
    while a < math.tau - 0.15:
        back = 0.5 + 0.5 * math.sin(a)
        s = (0.3 + 0.1 * back) * rng.uniform(0.85, 1.2)
        d = RW + 0.38
        x, y = math.cos(a) * d, math.sin(a) * d
        h = (0.2 + 0.26 * back) * rng.uniform(0.85, 1.15)
        clip_below(bp.rock("stone_%d" % k, (s, s * 0.85, h), (x, y, h * 0.35),
                           ("pm_seastone", "pm_seastone_lt", "pm_seastone_dk")[k % 3], rng))
        top = h * 0.35 + h * 0.75
        kind = k % 3
        if kind == 0:
            coral("coral_%d" % k, (x, y, top - 0.05), rng, depth=2, length=0.24 + 0.14 * back, r=0.07, spread=0.7)
        elif kind == 1:
            mound("brain_%d" % k, 0.2 + 0.05 * back, (x + 0.05, y - 0.04, top - 0.12), "pm_teal", (1.0, 0.9, 0.8))
            torus("brain_ridge_%d" % k, 0.1 + 0.03 * back, 0.022, (x + 0.05, y - 0.04, top - 0.12 + 0.1),
                  "pm_teal_lt", scale=(1.0, 0.9, 1.0))
        a += (s * 1.5) / d
        k += 1
    # A clam open on the rim, a pearl in it.
    cx, cy = math.cos(4.1) * (RW + 0.12), math.sin(4.1) * (RW + 0.12)
    mound("clam", 0.22, (cx, cy, 0.0), "pm_pearl_blue", (1.3, 1.0, 0.45))
    sphere("clam_pearl", 0.085, (cx, cy - 0.04, 0.12), "pm_pearl", emit=0.3)
    return (5.0, WELL)


# =================================================================================
#  The Firmament (air)
# =================================================================================

def streamer(name, pts, r, colour, emit=0.0):
    """A ribbon of moving air along a run of points: a line of rods, fatter
    in the middle of its run and thin at its ends."""
    n = len(pts) - 1
    for k, (p, q) in enumerate(zip(pts, pts[1:])):
        t = (k + 0.5) / n
        rr = r * (0.45 + 0.55 * math.sin(t * math.pi))
        rod("%s_%d" % (name, k), p, q, rr, colour, verts=6, emit=emit)
        if k:
            sphere("%s_j_%d" % (name, k), rr, p, colour, emit=emit)


def sweep(name, path, radii, bands, around=16, depth=0.82, rng=None, jit=0.0):
    """One round body swept along a path in the x-z plane, as thick at each
    point as `radii` says -- an arch, a leg, a limb -- coloured in level
    bands by height, so bedding runs straight across it however it curves.
    `bands` is a list of (top z, colour), lowest first."""
    n = len(path)
    side = Vector((0.0, 1.0, 0.0))
    verts = []
    for i in range(n):
        p = Vector(path[i])
        t = (Vector(path[min(i + 1, n - 1)]) - Vector(path[max(i - 1, 0)])).normalized()
        nrm = t.cross(side).normalized()
        wob = [1.0 + (rng.uniform(-jit, jit) if rng else 0.0) for _ in range(around)]
        for j in range(around):
            a = j / around * math.tau
            verts.append(p + (nrm * math.cos(a) + side * math.sin(a) * depth) * radii[i] * wob[j])
    faces = []
    for i in range(n - 1):
        for j in range(around):
            j2 = (j + 1) % around
            faces.append((i * around + j, i * around + j2, (i + 1) * around + j2, (i + 1) * around + j))
    faces.append(tuple(range(around - 1, -1, -1)))
    faces.append(tuple((n - 1) * around + j for j in range(around)))
    colours = []
    for _, c in bands:
        if c not in colours:
            colours.append(c)
    ob = mesh_object(name, verts, faces, colours[0], rough=0.9)
    for m, c in enumerate(colours[1:], 1):
        ob.data.materials.append(bp.material("%s_%d" % (name, m), c, 0.9))
    for poly in ob.data.polygons:
        z = sum(verts[v].z for v in poly.vertices) / len(poly.vertices)
        c = bands[-1][1]
        for top, col in bands:
            if z < top:
                c = col
                break
        poly.material_index = colours.index(c)
    return ob


def prop_wind_arch():
    """An arch of pale stone the wind has worn through: two legs pinched in at
    the waist and swelling again where the span leaves them, the span
    thinning to its middle, and its bedding running level right across it,
    cream and grey. A ribbon of pale blue air threads through under it."""
    rng = random.Random(401)
    X, ZS, RISE = 1.15, 1.5, 0.82
    path, radii = [], []
    for k in range(7):                                  # up the left leg
        z = ZS * k / 6
        path.append((-X, 0.0, z))
        radii.append(0.55 - 0.24 * math.sin(min(1.0, z / ZS * 1.1) * math.pi) + 0.12 * (z / ZS))
    for k in range(1, 16):                              # over the top
        ph = math.pi - math.pi * k / 16
        path.append((X * math.cos(ph), 0.0, ZS + RISE * math.sin(ph)))
        radii.append(0.43 - 0.15 * math.sin(math.pi * k / 16))
    for k in range(7):                                  # down the right leg
        z = ZS * (1 - k / 6)
        path.append((X, 0.0, z))
        radii.append(0.53 - 0.22 * math.sin(min(1.0, z / ZS * 1.1) * math.pi) + 0.12 * (z / ZS))
    L, M, D = "pm_pale_lt", "pm_pale", "pm_pale_dk"
    # Dark beds every so often and thick enough to show: pale on pale alone
    # quantises to one colour, and the arch was a smooth pipe.
    bands = [(0.2, M), (0.4, D), (0.7, L), (0.86, D), (1.12, L), (1.3, M), (1.46, D), (1.72, L), (1.9, D),
             (2.1, L), (2.24, D), (9.0, L)]
    ob = sweep("arch", path, radii, bands, rng=rng, jit=0.13)
    flat(ob)
    # Fallen pieces and a pad of bare rock under it.
    for k, (x, y, s) in enumerate(((-1.8, -0.3, 0.16), (-0.42, -0.45, 0.11), (0.5, -0.35, 0.13), (1.75, -0.25, 0.15),
                                   (0.05, 0.3, 0.12))):
        bp.rock("fallen_%d" % k, (s * 1.3, s, s * 0.7), (x, y, s * 0.3), (M, L, D)[k % 3], rng)
    mound("pad", 0.5, (0, 0.02, 0), D, (3.9, 1.2, 0.1))
    # The wind through it: a pale blue ribbon curling under the span.
    pts = [(-1.9 + 3.8 * t, -0.3 + 0.12 * math.sin(t * 6.0), 0.95 + 0.5 * math.sin(t * math.pi * 1.6))
           for t in [i / 16 for i in range(17)]]
    streamer("breeze", pts, 0.06, "pm_sky", emit=0.55)
    return 4.0


def prop_cloud_pillar():
    """A pillar of cloud standing up off the ground, turning: puffs of white
    climbing round and round each other, shadowed grey-blue beneath, a pale
    blue band of wind wound round it, a bank of cloud at its foot and the top
    of it spreading."""
    rng = random.Random(402)
    # Three strands of puffs wound round one another like a rope, so the
    # turning shows in the shape itself; a heap of round puffs is a snowman.
    H, TURNS = 1.9, 1.25
    for s in range(3):
        for i in range(11):
            t = i / 10
            a = s / 3 * math.tau + t * math.tau * TURNS
            rr = 0.25 + 0.1 * max(0.0, t - 0.7) / 0.3
            r = (0.27 + 0.05 * math.sin(i * 1.7 + s)) * (1.0 + 0.3 * max(0.0, t - 0.75) / 0.25)
            col = ("pm_cloud", "pm_cloud", "pm_cloud_sh")[s] if i % 4 else "pm_cloud_sh"
            blob("puff_%d_%d" % (s, i), r, (math.cos(a) * rr, math.sin(a) * rr * 0.85, 0.34 + t * H), col,
                 (1.15, 1.0, 0.82))
    # A core so no daylight shows through it.
    cyl("core", 0.26, H, (0, 0, 0.3 + H / 2), "pm_cloud_sh", verts=12)
    # The wind wound round it in the grooves between the strands.
    for b in range(2):
        pts = []
        for i in range(25):
            t = i / 24
            a = b * math.pi + math.pi / 3 + t * math.tau * TURNS
            rr = 0.48 + 0.06 * math.sin(t * math.pi)
            pts.append((math.cos(a) * rr, math.sin(a) * rr * 0.85, 0.36 + t * (H - 0.2)))
        streamer("band_%d" % b, pts, 0.065, "pm_sky", emit=0.55)
    # Wisps torn off the top, curling away to the sides (not behind it, where
    # they stand higher in the picture than anything).
    for k, (a, ln) in enumerate(((-0.3, 0.5), (2.9, 0.42), (4.6, 0.3))):
        pts = [(math.cos(a + t * 0.9) * (0.45 + t * ln), math.sin(a + t * 0.9) * (0.45 + t * ln) * 0.85,
                H + 0.1 + t * 0.18) for t in [i / 6 for i in range(7)]]
        streamer("wisp_%d" % k, pts, 0.07, "pm_cloud", emit=0.2)
    # The bank of cloud it rises from.
    for k in range(9):
        a = k / 9 * math.tau + 0.3
        d = 0.42 + 0.12 * (k % 2)
        mound("bank_%d" % k, 0.3, (math.cos(a) * d, math.sin(a) * d * 0.8, 0.0), "pm_cloud" if k % 2 else "pm_cloud_sh",
              (1.5, 1.2, 0.85))
    return 3.0


def prop_sky_rock():
    """A chunk of pale rock hanging in the air a little over its own shadow:
    a flat top, an underside tapering to a point the way a floating island's
    does, its bedding showing as bands, a pale blue crystal in its face that
    is what holds it up, and two pebbles adrift beside it."""
    rng = random.Random(403)
    S = 1.2                          # the rock, a size up from first drawn
    GAP = 0.36                       # from the ground to the point of its keel
    before = set(bpy.context.scene.objects)
    # A broken-off lump of the ground, not a cut stone: a flat lumpy top, and
    # under it boulders clinging together, fewer, smaller and darker toward
    # the keel. Grey, not near-white: white facets catch the warm key and the
    # cool fill by turns and the rock comes out an opal.
    for k, (x, y, z, sx, sy, sz, col) in enumerate((
            (0.0, 0.0, 0.96, 0.68, 0.52, 0.16, "pm_pale"), (-0.34, 0.05, 0.98, 0.34, 0.3, 0.14, "pm_pale_dk"),
            (0.36, -0.04, 0.95, 0.32, 0.3, 0.13, "pm_pale"),
            (-0.22, 0.02, 0.76, 0.42, 0.36, 0.22, "pm_pale_dk"), (0.24, 0.0, 0.74, 0.4, 0.34, 0.22, "pm_pale_sh"),
            (0.0, -0.12, 0.7, 0.36, 0.3, 0.22, "pm_pale_dk"),
            (-0.08, 0.0, 0.5, 0.3, 0.26, 0.2, "pm_pale_sh"), (0.14, -0.04, 0.48, 0.24, 0.22, 0.18, "pm_pale_sh"),
            (0.02, 0.0, 0.3, 0.16, 0.15, 0.18, "pm_pale_sh"))):
        bp.rock("lump_%d" % k, (sx * S, sy * S, sz * S), (x * S, y * S, z * S), col, rng)
    pyramid("keel", (0.02 * S, 0.0, 0.24 * S), 0.12 * S, -0.2 * S, 5, "pm_pale_sh", rng=rng, wobble=0.12)
    crystal("heart", (0.1 * S, -0.44 * S, 0.66 * S), (0.17 * S, -0.62 * S, 0.86 * S), 0.09, "pm_sky",
            alt="pm_pale_blue", emit=0.8)
    crystal("heart_b", (0.02 * S, -0.42 * S, 0.62 * S), (-0.08 * S, -0.58 * S, 0.78 * S), 0.07, "pm_sky",
            alt="pm_pale_blue", emit=0.8)
    for k, (x, y, z, s) in enumerate(((0.86, -0.1, 0.62, 0.12), (-0.84, 0.12, 0.92, 0.1))):
        bp.rock("pebble_%d" % k, (s, s * 0.9, s * 0.8), (x * S, y, z * S), "pm_pale" if k else "pm_pale_dk", rng)
    # A wisp of cloud caught on it, on one side: what says "in the sky".
    for k, (x, y, z, r) in enumerate(((-0.5, -0.2, 0.62, 0.17), (-0.72, -0.12, 0.58, 0.13), (-0.3, -0.3, 0.56, 0.12),
                                      (0.56, 0.1, 0.84, 0.12), (0.72, 0.16, 0.86, 0.09))):
        blob("wisp_%d" % k, r * S, (x * S, y * S, z * S), "pm_cloud" if k % 2 == 0 else "pm_cloud_sh", (1.5, 1.0, 0.75))
    group(before, loc=(0, 0, GAP - 0.04 * S))
    # Its shadow: as wide as the rock, so nothing of the rock hangs past it
    # (the post pass would draw a shadow in the air under any part that did).
    cyl("shadow", 0.8, 0.01, (0.03, 0.02, 0.005), "pm_shadow", verts=28).scale = (1.0, 0.6, 1.0)
    return 2.25


def vortex(prefix, z0, z1, r0, r1, turns, arms, rng, colours=("pm_cloud", "pm_sky"), emit=(0.3, 0.6), r=0.05):
    """A funnel of wind: streamers wound up and round it, widening as they
    rise, with dust caught in them."""
    for b in range(arms):
        pts = []
        for i in range(31):
            t = i / 30
            a = b / arms * math.tau + t * math.tau * turns
            rr = r0 + (r1 - r0) * t ** 1.3
            pts.append((math.cos(a) * rr, math.sin(a) * rr * 0.9, z0 + (z1 - z0) * t))
        streamer("%s_%d" % (prefix, b), pts, r * (1.0 if b % 2 else 0.85), colours[b % 2], emit=emit[b % 2])
        for j in (8, 17, 25):
            p = pts[j]
            bp.rock("%s_dust_%d_%d" % (prefix, b, j), (0.04, 0.03, 0.03), (p[0] * 1.08, p[1] * 1.08, p[2] + 0.04),
                    "pm_dust", rng)


def prop_gale_well():
    """The Firmament's wellspring: a round floor of pale flagstones, a ring of
    tall pale standing stones round it, open at the front, each with a rune
    lit pale blue on the face it turns inward -- and in the middle a vortex
    of air standing up off the floor, streamers of white and pale blue wound
    up it and widening as they climb, dust and leaves caught in it."""
    rng = random.Random(404)
    RA = 2.0
    # A floor of one pale stone scoured smooth -- not flagged, as the Bedrock's
    # is -- with a kerb, and spiral grooves worn in it by the wind going round.
    cyl("floor", RA, 0.1, (0, 0, 0.05), "pm_pale", verts=40)
    annulus("kerb", RA - 0.16, RA + 0.02, 0.0, 0.13, "pm_pale_dk", sides=40)
    for arm in range(5):
        pts = []
        for i in range(19):
            t = i / 18
            r = 0.6 + (RA - 0.85) * t
            a = arm / 5 * math.tau + 1.6 * math.log(r / 0.6)
            pts.append((math.cos(a) * r, math.sin(a) * r, 0.1))
        crack("groove_%d" % arm, pts, 0.03, "pm_pale_dk", emit=0.0)
    # The eye: a round of pale blue stone the wind has scoured, rings worn in it.
    cyl("eye", 0.56, 0.12, (0, 0, 0.06), "pm_pale_blue", verts=28)
    for k, r in enumerate((0.44, 0.28)):
        torus("eye_ring_%d" % k, r, 0.025, (0, 0, 0.12), "pm_sky", emit=0.5)
    vortex("gale", 0.12, 2.7, 0.18, 1.05, 1.25, 6, rng)
    vortex("gale_in", 0.12, 1.6, 0.1, 0.42, 1.5, 3, rng, colours=("pm_sky", "pm_cloud"), emit=(0.6, 0.3), r=0.04)
    # The ring of standing stones, open at the front.
    n = 8
    for k in range(n):
        a = math.radians(-90 + 30) + k * math.radians(360 - 60) / (n - 1)
        x, y = math.cos(a) * 1.72, math.sin(a) * 1.72
        before = set(bpy.context.scene.objects)
        h = 1.25 + 0.3 * (0.5 + 0.5 * math.sin(a)) + rng.uniform(-0.05, 0.05)
        ob, front = standing_slab("stone_%d" % k, 0.4, 0.26, h, ("pm_pale", "pm_pale_lt", "pm_pale_dk")[k % 3], rng,
                                  taper=0.7, slant=0.1)
        rune("stone_rune_%d" % k, lambda xx, zz, h=h: front(xx, h * 0.6 + zz), ("arrow", "fork", "zig", "eye", "hook",
             "tri", "step", "arrow")[k], 0.22, "pm_sky", emit=0.8, r=0.026)
        group(before, rot=(0, 0, a - math.pi / 2), loc=(x, y, 0.06))
    return (5.0, WELL)


# =================================================================================
#  The Tempest (lightning)
# =================================================================================

def zigzag(rng, p0, p1, segs, jag, side=None):
    """Points from p0 to p1 knocked aside at every joint, the way lightning
    goes: each joint pushed off the line by up to `jag` of a segment's
    length, alternately one way and the other."""
    a, b = Vector(p0), Vector(p1)
    d = b - a
    ln = d.length
    w = d.normalized()
    ref = Vector((0.0, 1.0, 0.0)) if abs(w.y) < 0.9 else Vector((1.0, 0.0, 0.0))
    u = w.cross(ref).normalized()                    # mostly in the plane the camera sees
    v = w.cross(u).normalized()
    pts = [a]
    for i in range(1, segs):
        s = (1 if i % 2 else -1) * (side or 1)
        off = u * s * rng.uniform(0.4, 1.0) * jag * ln / segs + v * rng.uniform(-0.3, 0.3) * jag * ln / segs
        pts.append(a + d * (i / segs) + off)
    pts.append(b)
    return [tuple(p) for p in pts]


def bolt(name, p0, p1, r, rng, segs=6, jag=0.8, colour="pm_bolt", emit=1.2, forks=1, core=None):
    """A stroke of lightning from p0 to p1, forking. `core` lays a paler,
    thinner line down the middle of the main stroke."""
    pts = zigzag(rng, p0, p1, segs, jag)
    crack(name, pts, r, colour, emit=emit)
    if core:
        crack(name + "_core", [(p[0], p[1] - r * 0.6, p[2]) for p in pts], r * 0.5, core, emit=emit + 0.2)
    for f in range(forks):
        i = rng.randint(1, max(1, len(pts) - 3))
        p = Vector(pts[i])
        d = (Vector(p1) - Vector(p0)).normalized()
        off = Vector((rng.choice((-1, 1)) * 0.8, rng.uniform(-0.2, 0.2), rng.uniform(-0.3, 0.3)))
        q = p + (d + off).normalized() * (Vector(p1) - Vector(p0)).length * rng.uniform(0.2, 0.35)
        crack("%s_fork_%d" % (name, f), zigzag(rng, tuple(p), tuple(q), 3, jag), r * 0.7, colour, emit=emit)
    return pts


def fulgurite(prefix, base, rng, depth=3, length=0.62, r=0.13, trunk=1, veins=2):
    """Sand fused to glass where lightning went into the ground, standing up
    out of it as the bolt went: a trunk that jinks and forks into jagged
    branches, every length a six-sided tube of smoky glass with its facets
    light and dark by turns, knobbed where the lengths join, and pale yellow
    light still caught in a vein up the front of each. Ends in points."""
    count = [0]

    def grow(p, d, ln, rr, level, side, run):
        i = count[0]
        count[0] += 1
        perp = Vector((d.z, 0.0, -d.x)).normalized() * side
        mid = p + d * ln * 0.5 + perp * ln * 0.2
        q = p + d * ln
        # Grey glass, not brown, and joined at sharp elbows, not knobs: brown
        # and knobbed, it was a dead bush. In the thick old glass the light
        # shows through every other facet, as if lit from inside.
        lit = level >= depth - veins + 1
        for k, (a, b, rr_) in enumerate(((p, mid, rr), (mid, q, rr * 0.92))):
            ob = crystal("%s_%d_%d" % (prefix, i, k), tuple(a), tuple(b + (b - a).normalized() * rr_ * 0.5), rr_,
                         "pm_fulg", alt="pm_fulg_glow" if lit else ("pm_fulg_lt", "pm_fulg_dk")[k], point=0.0,
                         rough=0.18, spin=0.3 + 0.5 * k)
            if lit:
                ob.data.materials[1] = bp.material("%s_%d_%d_lit" % (prefix, i, k), "pm_fulg_glow", 0.18, 0.0, 0.45)
        if lit:
            crack("%s_vein_%d" % (prefix, i), [(v.x, v.y - rr * 0.95, v.z) for v in (p, mid, q)],
                  max(0.018, rr * 0.16), "pm_bolt", emit=1.0)
        if run > 1:                                    # the trunk jinks on up before it forks
            grow(q, (d + perp * 0.25).normalized(), ln * 0.92, rr * 0.94, level, -side, run - 1)
            return
        if level == 0:
            crystal("%s_tip_%d" % (prefix, i), tuple(q), tuple(q + d * ln * 0.45), rr * 0.85, "pm_fulg_lt",
                    alt="pm_fulg", point=1.0, rough=0.25)
            return
        for c, s in enumerate((-1, 1)):
            ang = s * rng.uniform(0.38, 0.62)
            nd = Vector((d.x * math.cos(ang) + d.z * math.sin(ang), rng.uniform(-0.15, 0.15),
                         -d.x * math.sin(ang) + d.z * math.cos(ang))).normalized()
            if nd.z < 0.35:
                nd = (nd + Vector((0.0, 0.0, 0.6))).normalized()
            grow(q, nd, ln * rng.uniform(0.74, 0.86), rr * 0.74, level - 1, -s, 1)
    grow(Vector(base), Vector((0.05, 0.0, 1.0)).normalized(), length, r, depth, 1, trunk)


def prop_fulgurite_spire():
    """Fulgurite: glass the lightning made of the sand it struck, standing up
    out of a scorched patch in a branching spire taller than a man -- jagged
    where coral is round -- smoky brown-grey glass with pale yellow light
    still running in it."""
    rng = random.Random(501)
    fulgurite("glass", (0.0, 0.05, 0.0), rng, depth=2, length=0.74, r=0.17, trunk=2)
    fulgurite("glass_b", (0.44, 0.18, 0.0), rng, depth=1, length=0.5, r=0.1, veins=1)
    fulgurite("glass_c", (-0.42, 0.12, 0.0), rng, depth=1, length=0.42, r=0.09, veins=1)
    for k in range(6):
        a = k / 6 * math.tau + 0.3
        bp.rock("lump_%d" % k, (0.1, 0.08, 0.06), (math.cos(a) * 0.55, math.sin(a) * 0.36, 0.03),
                "pm_fulg_dk" if k % 2 else "pm_fulg", rng)
    mound("scorch", 0.5, (0, 0.05, 0), "pm_scorch", (1.7, 1.1, 0.08))
    mound("sand", 0.5, (0, 0.05, 0), "pm_slate_dk", (2.1, 1.35, 0.05))
    return 3.0


def prop_storm_rod():
    """A tall iron rod driven into a block of slate: banded with iron collars,
    a crown of three prongs at its top round a central point, and lightning
    crackling off the prongs into the air. The stone is scorched black where
    the rod goes in, and so is the ground round it."""
    rng = random.Random(502)
    clip_below(bp.rock("block", (0.5, 0.42, 0.42), (0.0, 0.0, 0.16), "pm_slate", rng))
    clip_below(bp.rock("block_b", (0.3, 0.26, 0.26), (0.36, -0.12, 0.08), "pm_slate_lt", rng))
    clip_below(bp.rock("block_c", (0.26, 0.24, 0.2), (-0.34, -0.1, 0.06), "pm_slate_dk", rng))
    cyl("burn", 0.22, 0.03, (0.0, -0.02, 0.5), "pm_scorch", verts=12)
    # Thick, or at eighty pixels the rod is a hairline.
    Z0, Z1 = 0.46, 1.86
    cyl("rod", 0.095, Z1 - Z0, (0, 0, (Z0 + Z1) / 2), "pm_iron", verts=10, metal=0.4)
    for k, z in enumerate((0.62, 1.1, 1.52)):
        cyl("collar_%d" % k, 0.13, 0.08, (0, 0, z), "pm_iron_lt", verts=10, metal=0.4)
    blk("rust", (0.04, 0.02, 0.36), (0.06, -0.09, 0.84), "pm_rust", bev=0)
    # The crown: three prongs bent out and up round a centre point.
    spike("point", (0, 0, Z1 - 0.02), (0, 0, Z1 + 0.36), 0.09, "pm_iron_lt", verts=8)
    tips = []
    for k in range(3):
        a = math.radians(-90) + k * math.tau / 3
        tip = (math.cos(a) * 0.3, math.sin(a) * 0.22, Z1 + 0.2)
        rod("prong_%d" % k, (0, 0, Z1 - 0.06), (math.cos(a) * 0.22, math.sin(a) * 0.16, Z1 - 0.02), 0.04, "pm_iron",
            verts=6)
        spike("prong_tip_%d" % k, (math.cos(a) * 0.22, math.sin(a) * 0.16, Z1 - 0.04), tip, 0.045, "pm_iron", verts=6)
        tips.append(tip)
    tips.append((0, 0, Z1 + 0.36))
    # The crackle: a white-hot spark on the point, and forked strokes off it
    # and the prongs, out into the air and one crawling down the rod.
    sphere("spark", 0.09, (0, 0, Z1 + 0.38), "pm_bolt_core", emit=1.4)
    for k, (t, (dx, dz)) in enumerate(zip(tips, ((-0.5, 0.06), (0.46, 0.2), (0.16, -0.3), (-0.2, 0.32)))):
        bolt("arc_%d" % k, t, (t[0] + dx, t[1] - 0.05, t[2] + dz), 0.03, rng, segs=4, jag=1.0, emit=1.2,
             forks=1 if k % 2 == 0 else 0)
    crack("crawl", zigzag(rng, (0.1, -0.06, Z1 - 0.1), (0.1, -0.06, Z1 - 0.62), 4, 0.6), 0.025, "pm_bolt", emit=1.2)
    mound("scorch", 0.5, (0, 0.02, 0), "pm_scorch", (1.5, 1.05, 0.07))
    return 2.5


def prop_thunder_stone():
    """A standing stone the lightning struck: slate grey, its broken top
    burnt black, and down the face of it the scar -- a jagged black burn
    with the strike still glowing pale yellow along the middle of it, forking
    where it ran. Scorched ground round its foot, and glassy lumps where the
    strike went into the ground."""
    rng = random.Random(503)
    # Broad and flat-faced: narrower, with a round black top, it read as a can.
    W, T, H = 0.98, 0.44, 1.78
    ob, front = standing_slab("stone", W, T, H, "pm_slate", rng, taper=0.8, slant=0.36)
    # The burnt top: black over the high corner where it was struck, and
    # running down the face a little way.
    blob("burnt", 0.5, (-0.18, 0.0, H - 0.06), "pm_scorch", (W * 0.6, T * 1.1, 0.26))
    blob("burnt_b", 0.5, (0.04, -0.04, H - 0.24), "pm_scorch", (W * 0.34, T * 1.08, 0.3))
    # The scar: a zigzag down the face, black burn either side of a glowing line.
    zz = [(0.06, H - 0.12), (-0.1, H - 0.4), (0.1, H - 0.66), (-0.06, H - 0.92), (0.12, H - 1.2), (-0.04, H - 1.45),
          (0.05, 0.12)]
    # The glowing line three pixels wide: at two, the downsample mixes it
    # with the black either side and it no longer counts as lit at night.
    crack("burn", [front(x, z, 0.012) for x, z in zz], 0.085, "pm_scorch", emit=0.0)
    crack("scar", [front(x, z, 0.06) for x, z in zz], 0.045, "pm_bolt", emit=1.15)
    crack("scar_fork", [front(-0.1, H - 0.4, 0.06), front(-0.24, H - 0.6, 0.06), front(-0.3, H - 0.78, 0.06)], 0.03,
          "pm_bolt", emit=1.15)
    crack("scar_fork_b", [front(0.12, H - 1.2, 0.06), front(0.26, H - 1.34, 0.06)], 0.03, "pm_bolt", emit=1.15)
    # The ground: scorched, and fused glass where the strike went in.
    mound("scorch", 0.5, (0.0, -0.05, 0), "pm_scorch", (1.6, 1.1, 0.07))
    for k, (x, y) in enumerate(((-0.42, -0.38), (0.4, -0.34), (0.1, -0.5))):
        crystal("glass_%d" % k, (x, y, 0.0), (x * 1.2, y - 0.05, 0.16 + 0.04 * k), 0.05, "pm_fulg", alt="pm_fulg_lt",
                n=5, point=0.4, rough=0.25)
    for k in range(5):
        a = math.pi + k / 4 * math.pi
        bp.rock("rubble_%d" % k, (0.1, 0.08, 0.06), (math.cos(a) * 0.55, math.sin(a) * 0.32, 0.03),
                "pm_slate_dk" if k % 2 else "pm_slate", rng)
    return 2.25


def prop_storm_well():
    """The Tempest's wellspring: a ring of black stones like broken teeth,
    each with a spark at its point, round a scorched floor split by glowing
    cracks running out from the middle -- and over the middle, hanging in the
    air, a sphere of lightning: a white-hot heart in a cage of crackling
    strokes, and bolts jumping from it to the stones."""
    rng = random.Random(504)
    RA = 1.95
    mound("floor", 1.0, (0, 0, 0), "pm_slate_dk", (RA, RA, 0.1))
    cyl("scorch", 1.25, 0.02, (0, 0, 0.1), "pm_scorch", verts=32)
    # Cracks split out from under it, a few and not evenly -- nine even ones
    # made a sun's rays round a halo.
    for k, (a, ln) in enumerate(((0.3, 1.5), (1.9, 1.3), (3.3, 1.5), (4.7, 1.4))):
        p0 = (math.cos(a) * 0.1, math.sin(a) * 0.1, 0.12)
        p1 = (math.cos(a + 0.25) * ln, math.sin(a + 0.25) * ln, 0.11)
        # Gently jagged: lying flat and seen from above, a sharp zigzag falls
        # apart into separate chevrons.
        bolt("ground_%d" % k, p0, p1, 0.03, rng, segs=5, jag=0.45, emit=0.75, forks=1)
    # The ring of black stones, jagged, leaning out a little; low in front.
    tips = []
    n = 9
    for k in range(n):
        a = (k + 0.5) / n * math.tau - math.pi / 2
        back = 0.5 + 0.5 * math.sin(a)
        d = 1.68
        h = 0.75 + 0.8 * back + rng.uniform(-0.08, 0.08)
        x, y = math.cos(a) * d, math.sin(a) * d
        lean = (math.cos(a) * 0.1, math.sin(a) * 0.1)
        pyramid("stone_%d" % k, (x, y, 0.0), 0.26 + 0.04 * back, h, 5, "pm_storm", lean=lean, face=a + math.pi,
                rng=rng, wobble=0.15, rough=0.5, alt="pm_slate_dk")
        tips.append((x + lean[0] * h, y + lean[1] * h, h))
        if k % 2 == 0:
            sphere("stone_spark_%d" % k, 0.05, (x + lean[0] * h, y + lean[1] * h, h + 0.02), "pm_bolt_core", emit=1.3)
    # The sphere: a small white-hot heart, and a cage of jagged strokes round
    # it that makes the ball -- a big solid ball was a sun.
    C = Vector((0.0, 0.0, 1.35))
    sphere("heart", 0.2, tuple(C), "pm_bolt_core", emit=1.3)
    sphere("glow", 0.27, tuple(C + Vector((0.0, 0.05, 0.0))), "pm_bolt_dk", emit=0.7)
    for ring in range(6):
        tilt, yaw = (0.35, 1.2, 0.8, 1.7, 2.3, 0.1)[ring], ring * 0.75
        pts = []
        for i in range(13):
            t = i / 12 * math.tau
            r = 0.5 + rng.uniform(-0.08, 0.08)
            p = Vector((math.cos(t) * r, 0.0, math.sin(t) * r))
            p.rotate(Euler((tilt, 0.0, yaw)))
            pts.append(tuple(C + p))
        crack("cage_%d" % ring, pts, 0.024, "pm_bolt", emit=1.2)
    # Bolts from it to three of the stones at the back and sides.
    for k, i in enumerate((2, 4, 6)):
        bolt("leap_%d" % k, tuple(C + (Vector(tips[i]) - C).normalized() * 0.5), tips[i], 0.026, rng, segs=6, jag=0.7,
             emit=1.2, forks=1)
    return (5.0, WELL)


# =================================================================================
#  The rift, at the Stronghold
# =================================================================================

def tear(name, w, h, z0, rng, rings=12, around=44, arms=3, twist=3.2):
    """A tear in the air, standing upright and facing the camera: a lens
    pointed top and bottom with a ragged dark rim, and inside it light
    swirling inward -- violet, then pink, then pale blue -- to a white core."""
    jag = [1.0 + rng.uniform(-0.1, 0.05) for _ in range(around)]

    def outline(a):
        # a = 0 at the top, going round; x swells to w/2 at the waist, pointed ends.
        s = math.sin(a)
        return (w / 2) * math.copysign(abs(s) ** 1.6, s), (h / 2) * math.cos(a)
    cz = z0 + h / 2
    verts = [(0.0, 0.0, cz)]
    for i in range(1, rings + 1):
        r = i / rings
        for j in range(around):
            a = j / around * math.tau
            ox, oz = outline(a)
            rr = r * (jag[j] if i == rings else 1.0)
            verts.append((ox * rr, -0.002 * i, cz + oz * rr))
    faces, mats = [], []
    for j in range(around):
        faces.append((0, 1 + j, 1 + (j + 1) % around))
        mats.append(4)
    for i in range(1, rings):
        rn = (i + 0.5) / rings
        for j in range(around):
            j2 = (j + 1) % around
            faces.append((1 + (i - 1) * around + j, 1 + i * around + j, 1 + i * around + j2, 1 + (i - 1) * around + j2))
            phase = (j + 0.5) / around * math.tau + twist * rn
            arm = ((phase * arms / math.tau) % 1.0) < 0.3
            if rn > 0.86:
                m = 0
            elif rn < 0.22:
                m = 4
            elif arm:
                m = 2 if rn > 0.5 else 3
            else:
                m = 1 if rn > 0.5 else 2
            mats.append(m)
    ob = mesh_object(name, verts, faces, "pm_tear_rim", rough=0.6, emit=0.4)
    for m, (col, e) in enumerate((("pm_tear_violet", 0.9), ("pm_tear_pink", 1.0), ("pm_tear_blue", 1.1),
                                  ("pm_tear_core", 1.3)), 1):
        ob.data.materials.append(bp.material("%s_%d" % (name, m), col, 0.6, 0.0, e))
    for p, m in zip(ob.data.polygons, mats):
        p.material_index = m
    return ob


def prop_primordial_rift():
    """The way into the Primordium, at the Stronghold: a round of the
    plateau's grey flagstones, and on it five standing stones in a ring, each
    of a different stone and cut with runes that burn its element's colour,
    with its element on its crown -- a flame on the black one, crystal on the
    brown, a pearl on the sea-grey, a twist of cloud on the pale, a crackle on
    the slate -- and in the middle, standing in the air, a tear of swirling
    light. The front of the ring is open: the way in is at the image's middle."""
    rng = random.Random(601)
    RA, RS = 2.3, 1.9
    annulus("bed", 0.0, RA, 0.0, 0.06, "pm_basalt_gdk")
    for k, (seed, poly) in enumerate(cells(scatter_seeds(rng, RA, 0.55), RA, 0.04)):
        if math.hypot(*seed) < 0.7:
            continue
        # Two close greys: the flags are only the ground the stones stand on.
        plate("flag_%d" % k, poly, 0.06, 0.11 + rng.uniform(0.0, 0.02), "pm_basalt_grey" if k % 3 else "pm_basalt_gmd")
    cyl("hearth", 0.72, 0.1, (0, 0, 0.06), "pm_basalt_gdk", verts=28)
    torus("hearth_ring", 0.62, 0.035, (0, 0, 0.11), "pm_tear_blue", emit=0.9)
    cyl("glow", 0.42, 0.02, (0, 0, 0.12), "pm_tear_violet", verts=24, emit=0.9)
    # The stones: (angle, stone, darker stone, rune colour, rune glow, crown)
    stones = (
        (90, "pm_pale_dk", "pm_pale_sh", "el_air", 0.9, "air"),
        (162, "pm_obsidian", "pm_obsidian_lt", "el_fire", 1.1, "fire"),
        (234, "pm_seastone", "pm_seastone_dk", "el_water", 1.0, "water"),
        (306, "pm_earth", "pm_earth_dk", "pm_amber", 1.0, "earth"),
        (18, "pm_slate", "pm_slate_dk", "el_lightning", 1.1, "lightning"),
    )
    for k, (deg, col, dk, glow, e, crown) in enumerate(stones):
        a = math.radians(deg)
        x, y = math.cos(a) * RS, math.sin(a) * RS
        h = 2.25 + (0.25 if deg == 90 else 0.0) + rng.uniform(-0.06, 0.06)
        before = set(bpy.context.scene.objects)
        ob, front = standing_slab("stone_%d" % k, 0.62, 0.34, h, col, rng, taper=0.82, slant=0.18)
        # Runes down both broad faces: the front stones turn their backs to
        # the camera, and every stone must show its colour.
        for f, flip in enumerate((1, -1)):
            def at(xx, zz, z=0.0, flip=flip):
                p = front(xx * flip, z + zz)
                return (p[0], p[1] * flip, p[2])
            for j, (zz, kind) in enumerate(((h * 0.74, ("tri", "eye", "zig", "fork", "arrow")[k]),
                                            (h * 0.48, ("hook", "step", "arrow", "zig", "eye")[k]),
                                            (h * 0.24, ("fork", "tri", "hook", "step", "zig")[k]))):
                rune("stone_rune_%d_%d_%d" % (k, f, j), lambda xx, z2, zz=zz, at=at: at(xx, z2, zz), kind, 0.26, glow,
                     emit=e, r=0.03)
        # Its element on its crown.
        top = h - 0.06
        if crown == "fire":
            fire("crown_%d" % k, 0.0, 0.0, top, 0.14, 0.4, emit=1.15, tongues=4, seed=40 + k)
        elif crown == "earth":
            for j, (dx, dy, hh) in enumerate(((0.0, 0.0, 0.34), (-0.12, 0.04, 0.22), (0.12, -0.02, 0.24))):
                crystal("crown_%d_%d" % (k, j), (dx, dy, top - 0.05), (dx * 1.8, dy, top + hh), 0.07, "pm_jade",
                        alt="pm_jade_dk", emit=0.4)
        elif crown == "water":
            sphere("crown_%d" % k, 0.13, (0.0, 0.0, top + 0.1), "pm_pearl", emit=0.3)
            torus("crown_cup_%d" % k, 0.12, 0.04, (0.0, 0.0, top + 0.02), "pm_teal")
        elif crown == "air":
            for j, (dx, dz, r) in enumerate(((0.0, 0.12, 0.13), (-0.12, 0.06, 0.1), (0.13, 0.08, 0.09))):
                blob("crown_%d_%d" % (k, j), r, (dx, 0.0, top + dz), "pm_cloud", (1.4, 1.0, 0.8))
        else:
            sphere("crown_%d" % k, 0.07, (0.0, 0.0, top + 0.08), "pm_bolt_core", emit=1.3)
            for j, (dx, dz) in enumerate(((-0.24, 0.2), (0.22, 0.26), (0.02, 0.32))):
                bolt("crown_arc_%d_%d" % (k, j), (0.0, 0.0, top + 0.08), (dx, -0.02, top + 0.08 + dz), 0.022, rng,
                     segs=3, jag=1.0, emit=1.2, forks=0)
        group(before, rot=(0, 0, a - math.pi / 2), loc=(x, y, 0.08))
    # The tear, standing in the middle of the ring.
    tear("tear", 1.05, 2.6, 0.32, rng)
    for k, (x, z, r) in enumerate(((-0.62, 1.1, 0.04), (0.66, 1.7, 0.035), (-0.5, 2.3, 0.03), (0.55, 0.7, 0.04))):
        sphere("mote_%d" % k, r * 1.4, (x, -0.05, z), ("pm_tear_pink", "pm_tear_blue", "pm_tear_core", "pm_tear_violet")[k],
               emit=1.1)
    return (6.0, BUILDING)


# =================================================================================
#  The Conflux
# =================================================================================

def prop_conflux_dais():
    """The heart of the Conflux: a round dais of grey stone going up in three
    steps all the way round, a gold ring let into its top, and inside the
    ring a five-pointed star inlaid in five lit colours, one element in each
    point -- fire at the north, then earth, water, air and lightning going
    round sunwise -- the point of the star where they meet burning white.
    Seen from high up, like everything that lies flat."""
    rng = random.Random(701)
    steps = ((3.4, 0.14, "pm_dais_dk"), (3.05, 0.28, "pm_dais"), (2.7, 0.42, "pm_dais_lt"))
    for k, (r, top, col) in enumerate(steps):
        ob = cyl("step_%d" % k, r, top, (0, 0, top / 2), col, verts=64)
        bp.bevel(ob, 0.03)
        # A paler edge along each tread, so every step reads as a step.
        annulus("nosing_%d" % k, r - 0.1, r - 0.02, top, top + 0.012, "pm_dais_lt" if k < 2 else "pm_basalt_glt",
                sides=64)
    Z = 0.42
    annulus("ring", 2.42, 2.56, Z, Z + 0.02, "pm_gold", sides=64)
    annulus("ring_in", 2.3, 2.36, Z, Z + 0.015, "pm_gold", sides=64)
    # Little marks round the band, between the ring's two lines.
    for k in range(20):
        a = k / 20 * math.tau
        blk("tick_%d" % k, (0.05, 0.12, 0.02), (math.cos(a) * 2.39, math.sin(a) * 2.39, Z + 0.01), "pm_gold",
            rot=(0, 0, a + math.pi / 2), bev=0)
    # The star: its five points, each a triangle drawn in a little from its
    # neighbours so a dark line shows between, and the pentagon in the middle.
    RO, RI = 2.22, 2.22 * 0.382
    tips = [math.radians(90 - k * 72) for k in range(5)]
    inner = [math.radians(90 - 36 - k * 72) for k in range(5)]
    cols = ("el_fire", "el_earth", "el_water", "el_air", "el_lightning")

    def pt(a, r):
        return (math.cos(a) * r, math.sin(a) * r)
    for k in range(5):
        tri = [pt(inner[k - 1], RI), pt(tips[k], RO), pt(inner[k], RI)]
        cx = sum(p[0] for p in tri) / 3
        cy = sum(p[1] for p in tri) / 3
        shrunk = []
        for p in tri:
            dx, dy = p[0] - cx, p[1] - cy
            d = math.hypot(dx, dy)
            f = (d - 0.07) / d
            shrunk.append((cx + dx * f, cy + dy * f))
        plate("point_%d" % k, shrunk, Z, Z + 0.04, cols[k], emit=0.6, rough=0.6)
    # The heart of the star in the rift's violet, the light of the place
    # between: white, it ran into air's white point.
    pent = [pt(inner[k], RI * 0.9) for k in range(5)]
    plate("heart", list(reversed(pent)), Z, Z + 0.045, "pm_tear_violet", emit=0.8, rough=0.6)
    sphere("jewel", 0.2, (0, 0, Z + 0.1), "pm_tear_core", emit=1.2)
    # A dark ground for the star to lie on, inside the ring.
    cyl("field", 2.3, 0.01, (0, 0, Z + 0.004), "pm_dais_dk", verts=64)
    _without_denoiser()              # the only prop here rendered at 2048px
    return (8.0, DAIS)


def prop_totem_quintessence():
    """The Quintessence's totem: a black stake, sharpened, banded in the
    violet of the place between, with five gems set in it, one for each
    element, and its eyes burning violet."""
    r = bp._totem("tt_quint", "tt_quint_b", "none", eye_emit=1.8)
    # The stake's point, standing up off the head.
    cone("stake", 0.12, 0.34, (0, 0.02, 1.02 + 0.17), "tt_quint", verts=6)
    # Five gems down the front, in the order of the dais: fire on top.
    for k, (x, z, col) in enumerate(((0.0, 1.12, "el_fire"), (-0.09, 0.58, "el_earth"), (0.09, 0.58, "el_water"),
                                     (-0.09, 0.25, "el_air"), (0.09, 0.25, "el_lightning"))):
        y = -0.13 if z > 1.0 else (-0.165 if z > 0.5 else -0.175)
        blk("gem_%d" % k, (0.1, 0.05, 0.1), (x, y, z), col, emit=1.3, bev=0, rot=(0, math.radians(45), 0))
    return r


# =================================================================================
#  Registration: name -> (builder, final pixels across)
# =================================================================================
PROPS = {
    "kiln_vent":          (_framed(prop_kiln_vent), 64),
    "obsidian_spire":     (_framed(prop_obsidian_spire), 96),
    "magma_well":         (_framed(prop_magma_well), 160),
    "cinder_heap":        (_framed(prop_cinder_heap), 48),
    "crystal_cluster":    (_framed(prop_crystal_cluster), 72),
    "monolith":           (_framed(prop_monolith), 112),
    "geode":              (_framed(prop_geode), 96),
    "stone_well":         (_framed(prop_stone_well), 160),
    "coral_spire":        (_framed(prop_coral_spire), 96),
    "kelp_stand":         (_framed(prop_kelp_stand), 72),
    "giant_shell":        (_framed(prop_giant_shell), 80),
    "tide_well":          (_framed(prop_tide_well), 160),
    "wind_arch":          (_framed(prop_wind_arch), 128),
    "cloud_pillar":       (_framed(prop_cloud_pillar), 96),
    "sky_rock":           (_framed(prop_sky_rock), 72),
    "gale_well":          (_framed(prop_gale_well), 160),
    "fulgurite_spire":    (_framed(prop_fulgurite_spire), 96),
    "storm_rod":          (_framed(prop_storm_rod), 80),
    "thunder_stone":      (_framed(prop_thunder_stone), 72),
    "storm_well":         (_framed(prop_storm_well), 160),
    "primordial_rift":    (_framed(prop_primordial_rift), 192),
    "conflux_dais":       (_framed(prop_conflux_dais), 256),
    # The totem keeps the family's own framing (bp._totem's), so it sits in
    # its slot in the bag the way every other boss's totem does.
    "totem_quintessence": (prop_totem_quintessence, 32),
}
