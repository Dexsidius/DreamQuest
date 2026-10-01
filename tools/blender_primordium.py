# =============================================================================
#  blender_primordium.py - the Elemental Conjures of the Primordium, where the
#  elements were before there was a world to put them in: fire, earth, water,
#  air and lightning, each in a lesser and a greater rank, and the
#  Quintessence, which is all five in one body.
#
#  Rendered by tools/make_creatures.ps1 like every other monster:
#      .\tools\make_creatures.ps1 -Only ember_conjure,inferno_conjure
#
#  Built out of the same parts as blender_creatures.py and fitted to a height
#  on screen the way blender_bestiary.py's monsters are (its Rig); registered
#  into the same CREATURES table, so a Conjure's sheet is made exactly the way
#  a rat's is. blender_creatures.py hands itself over at its foot.
#
#  They are not men in costumes. Each one is its element standing up: it does
#  not walk on legs so much as rise out of a heap of embers, a column of
#  water, a vortex, and the element moves on it all the time -- flames
#  flicker frame to frame, currents and orbiting stones turn -- because that
#  movement is most of what says "fire" or "wind" at fifty pixels. Every
#  greater one is the lesser grown huge, with something the lesser has not
#  got. Their deaths do not fall over: they come apart into what they were
#  made of.
#
#  Tools here the other rigs did not need:
#
#  * Bits: loose pieces (embers, rubble, droplets) that a pose can put
#    anywhere. A joint can only turn and swell, so a bit hangs two joints out
#    from where it starts: the first turns to point at where the bit is going
#    and swells to reach it, the second shrinks back by as much so the bit
#    itself keeps its size. A bit is a hundredth of its size -- gone -- in
#    every pose that does not place it.
#  * Faceted meshes: a rock is an icosphere with its corners pushed about and
#    flat faces, a crystal a pointed prism, a slab a chamfered block. The cel
#    bands fall on the facets the way light does on stone; an ellipsoid
#    shaded smooth is an egg.
#  * Flicker sets: lightning is built as several sets of bolts, hidden, and a
#    pose lights a different one or two every frame (lit()), so it crackles.
#  * Light geometry: a bolt, a streamer or a crack is one tube mesh, not a
#    capsule a segment, and the thin things are low in vertices; with the
#    shadow pool kept small (new_rig()), that is what lets the 192px sheets of
#    the Quintessence render on a GPU that is also running the desktop.
# =============================================================================

import math
import random

import bmesh
import bpy
from mathutils import Vector

import blender_character as bc
import blender_creatures as cr
import blender_bestiary as bb

E, C = cr.E, cr.C
X, fwd, sn, ease, phases, mix = cr.X, cr.fwd, cr.sn, cr.ease, cr.phases, cr.mix
gait = cr.gait
Rig = bb.Rig
struck, swing = bb.struck, bb.swing
TORUS = bc.mesh_torus


# --- colours ---------------------------------------------------------------------------
bc.PALETTE.update({
    # Fire: basalt gone black in the heat, and the flame in three bands -- a
    # yellow heart, orange round it, red at the tips. The flame itself is
    # light (a *_glow); the basalt and the coals are what give it a body.
    "pr_basalt": (0.24, 0.20, 0.21), "pr_basalt_dk": (0.14, 0.12, 0.13), "pr_basalt_lt": (0.36, 0.30, 0.29),
    "pr_ash": (0.30, 0.25, 0.24), "pr_ash_dk": (0.19, 0.16, 0.16),
    "pr_fire": (0.94, 0.40, 0.12), "pr_fire_md": (0.84, 0.30, 0.10), "pr_fire_dk": (0.70, 0.20, 0.08),
    "pr_flame_y_glow": (1.00, 0.88, 0.40), "pr_flame_o_glow": (1.00, 0.56, 0.14),
    "pr_flame_r_glow": (0.92, 0.27, 0.10), "pr_magma_glow": (1.00, 0.62, 0.18),
    "pr_ember_glow": (1.00, 0.48, 0.12), "pr_fire_eye_glow": (1.00, 0.98, 0.78),
    "pr_smoke": (0.56, 0.53, 0.54), "pr_smoke_dk": (0.42, 0.40, 0.42),
    # Earth: boulders of warm grey stone, and the Monolith's darker cut
    # stone; amber light in the joints and veins and runes; crystals of pale
    # green and of amber, each with a lit glint; moss, and roots.
    "pr_stone": (0.52, 0.47, 0.42), "pr_stone_dk": (0.38, 0.34, 0.31), "pr_stone_lt": (0.64, 0.59, 0.52),
    "pr_mono": (0.40, 0.40, 0.43), "pr_mono_dk": (0.28, 0.28, 0.32), "pr_mono_lt": (0.52, 0.52, 0.54),
    "pr_amber_glow": (1.00, 0.70, 0.22), "pr_amber_eye_glow": (1.00, 0.86, 0.42),
    "pr_xtal_g": (0.56, 0.86, 0.62), "pr_xtal_g_glow": (0.84, 1.00, 0.86),
    "pr_xtal_a": (0.96, 0.66, 0.24), "pr_xtal_a_glow": (1.00, 0.88, 0.56),
    "pr_moss": (0.40, 0.56, 0.24), "pr_moss_dk": (0.28, 0.42, 0.18), "pr_root": (0.42, 0.30, 0.20),
    # Water: deep blue under lighter water, foam, pale cyan light for eyes;
    # and what grows on the Maelstrom -- coral, rose and teal, and shell.
    "pr_water_dk": (0.11, 0.24, 0.52), "pr_water": (0.17, 0.40, 0.74), "pr_water_lt": (0.38, 0.65, 0.92),
    "pr_water_pale": (0.62, 0.84, 0.97), "pr_foam": (0.93, 0.97, 1.00), "pr_tide_eye_glow": (0.72, 1.00, 1.00),
    "pr_coral": (0.90, 0.48, 0.52), "pr_coral_teal": (0.30, 0.68, 0.64),
    "pr_shell": (0.95, 0.89, 0.82), "pr_shell_dk": (0.78, 0.68, 0.64),
    # Air: a faint core, streamers white, pale blue and grey-violet, a pale
    # mask with light behind its eyes, cloud, and what the wind has picked up.
    "pr_air_core": (0.80, 0.86, 0.95), "pr_air_mist": (0.70, 0.76, 0.88), "pr_air_white": (0.97, 0.98, 1.00),
    "pr_air_blue": (0.68, 0.82, 0.96), "pr_air_violet": (0.66, 0.63, 0.82),
    "pr_mask": (0.92, 0.91, 0.88), "pr_mask_dk": (0.36, 0.38, 0.50), "pr_gale_eye_glow": (0.80, 0.97, 1.00),
    "pr_cloud": (0.80, 0.82, 0.88), "pr_cloud_lt": (0.92, 0.93, 0.96), "pr_cloud_dk": (0.60, 0.63, 0.73),
    "pr_leaf": (0.50, 0.64, 0.26), "pr_leaf_dk": (0.36, 0.48, 0.20), "pr_leaf_brown": (0.70, 0.48, 0.22),
    "pr_dust": (0.72, 0.66, 0.56), "pr_splinter": (0.62, 0.46, 0.30),
    # Lightning: thundercloud, slate and violet-grey, and the bolts in it,
    # yellow-white and white-hot; the scorch a discharge leaves.
    "pr_storm": (0.38, 0.40, 0.50), "pr_storm_lt": (0.50, 0.51, 0.62), "pr_storm_dk": (0.25, 0.26, 0.35),
    "pr_storm_violet": (0.45, 0.41, 0.56),
    "pr_bolt_glow": (1.00, 0.95, 0.56), "pr_bolt_hot_glow": (1.00, 1.00, 0.88), "pr_storm_eye_glow": (1.00, 1.00, 0.80),
    "pr_scorch": (0.24, 0.22, 0.22), "pr_scorch_dk": (0.14, 0.13, 0.14),
    # The Quintessence: dark stone with a violet cast, and a white heart
    # with every element's light round it.
    "pr_quint": (0.27, 0.25, 0.34), "pr_quint_lt": (0.37, 0.35, 0.46), "pr_quint_dk": (0.16, 0.15, 0.22),
    "pr_prism_glow": (0.95, 0.91, 1.00), "pr_prism_hot_glow": (1.00, 1.00, 1.00),
})


# =================================================================================
#  The rig, rendered lean
# =================================================================================

def new_rig():
    """A Rig, and the render it is about to be drawn in told to keep EEVEE's
    shadow pool small. Nothing in a creature sheet casts a shadow (the sun's
    shadows are off; the shadow under a creature is a disc of its own), but
    EEVEE reserves the pool anyway, and at its default of 512MB it was a
    gigabyte of the 3.8GB the Quintessence's 192px sheets peaked at -- on a
    card sharing its memory with the desktop, the difference between
    rendering and Blender falling over. A sheet comes out byte for byte the
    same either way. (The setting stays on the scene for whatever is drawn
    after it in the same run, which is drawn the same either way too.)"""
    try:
        bpy.context.scene.eevee.shadow_pool_size = "16"
    except (AttributeError, TypeError):
        pass
    return Rig()


# =================================================================================
#  Meshes
# =================================================================================

def _faceted(me):
    for p in me.polygons:
        p.use_smooth = False
    return me


def rock(rx, ry, rz, seed=0, rough=0.16):
    """A boulder, a coal, a plate of basalt: an icosphere with every corner
    pushed in or out a little, drawn flat-faced."""
    key = ("rock", rx, ry, rz, seed, rough)
    if key in bc._meshes:
        return bc._meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.0)
    rnd = random.Random(seed * 7919 + 13)
    for v in bm.verts:
        k = 1.0 + rnd.uniform(-rough, rough)
        v.co = Vector((v.co.x * rx * k, v.co.y * ry * k, v.co.z * rz * k))
    me = bpy.data.meshes.new("rock")
    bm.to_mesh(me)
    bm.free()
    bc._meshes[key] = _faceted(me)
    return me


def prism(radius, length, sides=6, point=0.34):
    """A crystal: a prism of `sides` faces standing `length` up +Z from its
    root, drawn to a point over the last `point` of it."""
    key = ("prism", radius, length, sides, point)
    if key in bc._meshes:
        return bc._meshes[key]
    bm = bmesh.new()
    angles = [k * math.tau / sides for k in range(sides)]
    low = [bm.verts.new((radius * 0.8 * math.cos(a), radius * 0.8 * math.sin(a), 0.0)) for a in angles]
    high = [bm.verts.new((radius * math.cos(a), radius * math.sin(a), length * (1 - point))) for a in angles]
    apex = bm.verts.new((0.0, 0.0, length))
    bm.faces.new(list(reversed(low)))
    for k in range(sides):
        n = (k + 1) % sides
        bm.faces.new((low[k], low[n], high[n], high[k]))
        bm.faces.new((high[k], high[n], apex))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("prism")
    bm.to_mesh(me)
    bm.free()
    bc._meshes[key] = _faceted(me)
    return me


def slab(w, d, h, bevel=0.03):
    """A block of cut stone, centred on its origin, w x d x h across, its edges
    chamfered so each catches a band of light of its own: a sharp box at
    forty pixels is three flat colours, a chamfered one is carved."""
    key = ("slab", w, d, h, bevel)
    if key in bc._meshes:
        return bc._meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * w, v.co.y * d, v.co.z * h))
    if bevel > 0:
        try:
            bmesh.ops.bevel(bm, geom=list(bm.edges) + list(bm.verts), offset=bevel, segments=1, affect="EDGES",
                            profile=0.5, clamp_overlap=True)
        except TypeError:
            pass
    me = bpy.data.meshes.new("slab")
    bm.to_mesh(me)
    bm.free()
    bc._meshes[key] = _faceted(me)
    return me


def _lowcap(r_top, r_bot, length, segments=8, rings=6):
    """blender_character's capsule with a fifth of the vertices, cached under
    its own key: a bolt, a crack or a lick of flame is a pixel or three wide
    and cannot show the difference, and the Quintessence and the Thunder
    Conjure are made of hundreds of them -- at full detail, a 192px sheet of
    either was more than the GPU would hold, and Blender fell over."""
    key = ("lowcap", r_top, r_bot, length, segments, rings)
    if key in bc._meshes:
        return bc._meshes[key]
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=1.0)
    for v in bm.verts:
        x, y, z = v.co
        if z >= -1e-6:
            v.co = Vector((x * r_top, y * r_top, z * r_top))
        else:
            v.co = Vector((x * r_bot, y * r_bot, z * r_bot - length))
    me = bpy.data.meshes.new("lowcap")
    bm.to_mesh(me)
    bm.free()
    for poly in me.polygons:
        poly.use_smooth = True
    bc._meshes[key] = me
    return me


def thin(r, name, a, b, rad, colour, parent, r_tip=None):
    """Rig.limb for the thin things: the same tapered capsule from a to b,
    low in vertices (see _lowcap)."""
    a, b = Vector(a), Vector(b)
    d = b - a
    tip = rad if r_tip is None else r_tip
    length = max(0.001, d.length - rad * 0.2)
    ob = bc.part(name, _lowcap(rad, tip, length), colour, r.j[parent], loc=tuple(a))
    ob.rotation_mode = "QUATERNION"
    ob.rotation_quaternion = Vector((0, 0, -1)).rotation_difference(d.normalized())
    r.parts.append(ob)
    return ob


def _tube_mesh(points, radii, segments=6):
    """One mesh for a whole polyline: a ring of vertices round each point,
    joined ring to ring, capped with a point at each end. The rings are
    turned along the path by parallel transport, so the tube does not twist
    on itself at a kink."""
    key = ("tube", tuple((round(p.x, 4), round(p.y, 4), round(p.z, 4)) for p in points),
           tuple(round(q, 4) for q in radii), segments)
    if key in bc._meshes:
        return bc._meshes[key]
    n = len(points)
    dirs = []
    for i in range(n):
        d = points[min(n - 1, i + 1)] - points[max(0, i - 1)]
        dirs.append(d.normalized() if d.length > 1e-9 else Vector((0, 0, 1)))
    bm = bmesh.new()
    rings = []
    a = dirs[0].orthogonal().normalized()
    for i, p in enumerate(points):
        d = dirs[i]
        a = a - d * a.dot(d)
        a = a.normalized() if a.length > 1e-6 else d.orthogonal().normalized()
        b = d.cross(a).normalized()
        rings.append([bm.verts.new(p + (a * math.cos(k * math.tau / segments) + b * math.sin(k * math.tau / segments))
                                   * radii[i]) for k in range(segments)])
    for i in range(n - 1):
        for k in range(segments):
            k2 = (k + 1) % segments
            bm.faces.new((rings[i][k], rings[i][k2], rings[i + 1][k2], rings[i + 1][k]))
    for ring, p, q, rad in ((rings[0], points[0], points[1], radii[0]), (rings[-1], points[-1], points[-2], radii[-1])):
        tip = bm.verts.new(p + (p - q).normalized() * rad)
        for k in range(segments):
            bm.faces.new((ring[k], ring[(k + 1) % segments], tip))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new("tube")
    bm.to_mesh(me)
    bm.free()
    for poly in me.polygons:
        poly.use_smooth = True
    bc._meshes[key] = me
    return me


def tube(r, name, points, radii, colour, parent, segments=6):
    """A polyline drawn as one part: a bolt, a streamer, a crack -- one object
    where a capsule a segment made a dozen. The Quintessence was over three
    hundred parts, most of them the segments of things like these, and its
    192px sheets were more than the GPU would hold."""
    pts = [Vector(p) for p in points]
    # Drop points that sit on top of the one before: they would fold the tube.
    keep_p, keep_r = [pts[0]], [radii[0]]
    for p, q in zip(pts[1:], radii[1:]):
        if (p - keep_p[-1]).length > 1e-5:
            keep_p.append(p)
            keep_r.append(q)
    if len(keep_p) < 2:
        return None
    ob = bc.part(name, _tube_mesh(keep_p, keep_r, segments), colour, r.j[parent], loc=(0, 0, 0))
    r.parts.append(ob)
    return ob


def aim(r, ob, direction):
    """Turn a part whose length runs up its +Z to point along `direction`."""
    ob.rotation_mode = "QUATERNION"
    ob.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(Vector(direction).normalized())
    return ob


# =================================================================================
#  Bits: pieces a pose can put anywhere
# =================================================================================
U = 0.1         # how far a bit's second joint stands out from its first


class Bits:
    """A set of loose pieces, named tag0, tag1, ...: each hangs from two joints
    of its own under `parent`, starting where it is added."""

    def __init__(self, r, tag, parent="pose"):
        self.r, self.tag, self.parent, self.n = r, tag, parent, 0

    def add(self, origin, mesh, colour, rot=(0, 0, 0)):
        a, b = "%s%da" % (self.tag, self.n), "%s%db" % (self.tag, self.n)
        self.n += 1
        self.r.joint(a, origin, self.parent)
        self.r.joint(b, (0, 0, U), a)
        self.r.add("bit", mesh, colour, b, rot=rot)
        self.r.j[a].scale = (0.01, 0.01, 0.01)
        return self.n - 1


def put(v, tag, i, offset, size=1.0, spin=(0.0, 0.0, 0.0)):
    """Show bit i of `tag` at `offset` from where it was added, `size` times as
    big as it was made, turned by `spin`. Size 0 hides it."""
    a, b = "%s%da" % (tag, i), "%s%db" % (tag, i)
    off = Vector(offset)
    g = max(0.01, off.length / U)
    if off.length > 1e-6:
        e = Vector((0, 0, 1)).rotation_difference(off.normalized()).to_euler("XYZ")
        v[a] = tuple(math.degrees(c) for c in e)
    else:
        v[a] = (0.0, 0.0, 0.0)
    v["~" + a] = g - 1.0 if size > 0 else -1.0
    v[b] = spin
    # (Hidden at both joints: hidden only at the second, a bit thrown far
    # kept a hundredth of the first's swell and showed as a stray pixel.)
    v["~" + b] = size / g - 1.0 if size > 0 else -1.0


def thrown(p0, vel, t, grav=2.2, floor=0.03):
    """Where a piece thrown from p0 with velocity vel is t later: on a falling
    arc, and lying where it landed once it is down."""
    x0, y0, z0 = p0
    vx, vy, vz = vel
    z = z0 + vz * t - 0.5 * grav * t * t
    if z >= floor:
        return Vector((x0 + vx * t, y0 + vy * t, z))
    # When it came down: the later root of z(t) = floor.
    disc = vz * vz + 2.0 * grav * (z0 - floor)
    land = (vz + math.sqrt(max(0.0, disc))) / grav
    return Vector((x0 + vx * land, y0 + vy * land, floor))


# =================================================================================
#  Fire: tongues of flame, and how they move
# =================================================================================
FIRE = ("pr_flame_y_glow", "pr_flame_o_glow", "pr_flame_r_glow")


def tongue(r, name, parent, base, tip, width, cols=FIRE):
    """A tongue of flame on a joint of its own at `base`, reaching to `tip`:
    the yellow heart round its root, orange over the middle, red at the tip.
    Three tapered spikes, each shorter and fatter than the last, so each
    shows past the end of the one inside it. (The yellow is kept short and
    its end small: a capsule's round end reaches a radius past its length,
    and at the first try the heart was half of every tongue.)"""
    r.joint(name, base, parent)
    d = Vector(tip) - Vector(base)
    yel, ora, red = cols
    thin(r, "flame", (0, 0, 0), tuple(d), width * 0.86, red, name, r_tip=width * 0.12)
    thin(r, "flame", (0, 0, 0), tuple(d * 0.58), width * 0.94, ora, name, r_tip=width * 0.40)
    thin(r, "flame", (0, 0, 0), tuple(d * 0.24), width, yel, name, r_tip=width * 0.52)
    return name


def flicker(v, names, t, amp=8.0, swell=0.16, speed=1, lean=(0.0, 0.0), grow=0.0):
    """Every tongue in `names` turned and swelled a little differently each
    frame, so the outline of the fire never holds still. Whole numbers of
    cycles to a clip, so a loop comes round to its first frame; `lean` tips
    them all (negative X streams them back off a creature going forward) and
    `grow` swells them all."""
    for i, n in enumerate(names):
        p = (i * 0.618034) % 1.0
        a = amp * (0.6 * sn(t * speed, p) + 0.4 * sn(t * 2 * speed, p * 2.3))
        b = amp * (0.6 * sn(t * speed, p + 0.37) + 0.4 * sn(t * 3 * speed, p * 1.7))
        v[n] = (lean[0] + a, lean[1] + b, 0.0)
        v["~" + n] = grow + swell * (0.55 * sn(t * 2 * speed, p * 3.1) + 0.45 * sn(t * 3 * speed, p + 0.5))
    return v


def gutter(v, names, k):
    """Tongues dying down: k from 0 (as they were) to 1 (gone)."""
    for n in names:
        v["~" + n] = (1.0 + v.get("~" + n, 0.0)) * (1.0 - k) - 1.0
    return v


# =================================================================================
#  Fire: the Ember Conjure and the Inferno Conjure
# =================================================================================

def ember_heap(r, radius, seed=1, coals=12):
    """The heap of embers a fire Conjure stands up out of: a low mound of ash,
    coals heaped on it, some black and some still lit. The lit ones have
    black twins hidden in the same places, for when it goes out."""
    r.joint("base", (0, 0, 0))
    r.add("mound", rock(radius, radius * 0.92, radius * 0.30, seed, 0.10), "pr_ash_dk", "base",
          loc=(0, 0, radius * 0.10))
    r.joint("hot", (0, 0, 0), "base")
    r.joint("cold", (0, 0, 0), "base")
    r.j["cold"].scale = (0.01, 0.01, 0.01)
    rnd = random.Random(seed)
    for k in range(coals):
        a = k * math.tau / coals + rnd.uniform(-0.25, 0.25)
        d = radius * rnd.uniform(0.30, 0.90)
        s = radius * rnd.uniform(0.17, 0.27)
        z = radius * 0.10 + radius * 0.30 * max(0.0, 1 - (d / radius) ** 2) ** 0.5 + s * 0.15
        loc = (math.cos(a) * d, math.sin(a) * d, z)
        mesh = rock(s, s * 0.9, s * 0.72, seed * 31 + k)
        if k % 3 == 0:
            r.add("coal", mesh, "pr_ember_glow", "hot", loc=loc)
            r.add("coal", mesh, "pr_basalt_dk", "cold", loc=loc)
        else:
            r.add("coal", mesh, "pr_basalt" if k % 3 == 1 else "pr_ash", "base", loc=loc)


def crack(r, parent, points, width, colour="pr_magma_glow"):
    """A crack of light drawn across a surface, point to point: a seam in the
    basalt with the fire showing through it. Drawn on, not left as gaps
    between plates: a gap a pixel wide closes up in the reduction."""
    tube(r, "crack", points, [width] * len(points), colour, parent)


def flame_arm(r, side, sx, sh, upper, fore, w, flare, fire="pr_fire", hand="pr_basalt", claw="pr_basalt_dk",
              fist=False):
    """An arm of flame: two tapering lengths of fire with licks of it
    trailing up off the shoulder and the forearm, basalt over the wrist, and
    a hand of basalt -- three hooked claws on it, or for a `fist` a heavier
    block of a fist with the fire round it. Returns the flame joints for
    flicker()."""
    r.joint("shoulder_" + side, sh, "chest", rest=(0, -sx * flare, 0))
    r.limb("upper", (0, 0, 0), (0, 0, -upper), w, fire, "shoulder_" + side, r_tip=w * 0.85)
    r.joint("elbow_" + side, (0, 0, -upper), "shoulder_" + side, rest=(-12, 0, 0))
    r.limb("fore", (0, 0, 0), (0, 0, -fore), w * 0.88, fire, "elbow_" + side, r_tip=w * 0.80)
    r.joint("hand_" + side, (0, 0, -fore), "elbow_" + side)
    # Basalt over the wrist and the back of the hand: from the side an arm of
    # fire in front of a body of fire is not there at all.
    r.add("bracer", rock(w * 1.12, w * 1.05, fore * 0.30, 44 + (sx > 0), 0.10), hand, "elbow_" + side,
          loc=(0, 0, -fore * 0.72))
    names = []
    if fist:
        r.add("fist", rock(w * 1.45, w * 1.35, w * 1.35, 46 + (sx > 0), 0.12), hand, "hand_" + side,
              loc=(0, -w * 0.15, -w * 0.9))
        for k in (-1, 0, 1):
            r.add("knuckle", rock(w * 0.42, w * 0.36, w * 0.36, 50 + k), "pr_basalt_lt", "hand_" + side,
                  loc=(k * w * 0.72, -w * 1.30, -w * 1.25))
        crack(r, "hand_" + side, [(-w * 1.2, -w * 1.2, -w * 0.55), (0, -w * 1.32, -w * 0.62),
                                  (w * 1.2, -w * 1.2, -w * 0.5)], w * 0.20)
        for k in range(3):
            a = math.radians(-60 + k * 60)
            names.append(tongue(r, "lick_h%d%s" % (k, side), "hand_" + side,
                                (math.sin(a) * w * 0.9, w * 0.4, -w * 0.4),
                                (math.sin(a) * w * 1.6, w * 1.6, w * 1.2 + 0.04 * (k == 1)), w * 0.95))
    else:
        r.add("palm", rock(w * 1.15, w * 1.0, w * 1.05, 40 + (sx > 0)), hand, "hand_" + side,
              loc=(0, -w * 0.10, -w * 0.5))
        for k in (-1, 0, 1):
            thin(r, "claw", (k * w * 0.62, -w * 0.55, -w * 1.1), (k * w * 0.95, -w * 1.25, -w * 2.5), w * 0.36, claw,
                 "hand_" + side, r_tip=0.006)
            thin(r, "claw", (k * w * 0.95, -w * 1.25, -w * 2.5), (k * w * 1.0, -w * 2.0, -w * 2.9), w * 0.24, claw,
                 "hand_" + side, r_tip=0.004)
    # Licks trail up and back off the arm, so a raised or swung arm streams fire.
    names += [tongue(r, "lick_s" + side, "shoulder_" + side, (sx * 0.03, 0.03, 0.02), (sx * 0.10, 0.10, 0.24),
                     w * 1.15),
              tongue(r, "lick_u" + side, "shoulder_" + side, (sx * w * 0.6, w * 0.5, -upper * 0.55),
                     (sx * 0.13, 0.14, -upper * 0.55 + 0.12), w * 0.9),
              tongue(r, "lick_f" + side, "elbow_" + side, (sx * w * 0.5, w * 0.5, -fore * 0.55),
                     (sx * 0.12, 0.15, -fore * 0.55 + 0.10), w * 0.85)]
    return names


def fire_bits(r, info, plate=0.07):
    """The pieces a fire Conjure comes apart into, hidden until its death
    throws them: plates of its basalt, sparks, and smoke off the cold heap."""
    plates = Bits(r, "eplate")
    for i, (p0, _) in enumerate(info["plates"]):
        plates.add(p0, rock(plate, plate * 0.72, plate * 0.8, 60 + i, 0.15), "pr_basalt" if i % 2 else "pr_basalt_lt")
    sparks = Bits(r, "espark")
    for i, (p0, _) in enumerate(info["sparks"]):
        sparks.add(p0, E(plate * 0.5, plate * 0.5, plate * 0.5), "pr_flame_o_glow" if i % 2 else "pr_flame_y_glow")
    smoke = Bits(r, "esmoke")
    for i, (p0, _) in enumerate(info["smoke"]):
        smoke.add(p0, E(plate * (0.7 + 0.14 * i), plate * 0.7, plate), "pr_smoke" if i % 2 else "pr_smoke_dk")


def burst(p0s, z, spread, up):
    """Pieces thrown from round a chest at height z: (start, velocity) pairs
    spaced round the body, out and a little up, every other one higher."""
    out = []
    for k, (x, y) in enumerate(p0s):
        d = Vector((x, y, 0)).normalized() if (x or y) else Vector((0, 0, 0))
        out.append(((x, y, z + 0.06 * (k % 3)), (d.x * spread, d.y * spread * 0.9, up * (1.0 + 0.3 * (k % 2)))))
    return out


EMBER = {"tongues": []}     # its builder fills in the flame joints
EMBER["plates"] = burst([(-0.10, -0.12), (0.10, -0.12), (0.0, -0.14), (-0.10, 0.04), (0.10, 0.04), (0.0, 0.0),
                         (-0.14, -0.06), (0.14, -0.06)], 0.70, 0.66, 0.55)
EMBER["sparks"] = [((0.0, -0.04, 0.80), (math.sin(a) * 0.50, -math.cos(a) * 0.40, 1.5 + 0.4 * (k % 2)))
                   for k, a in enumerate(k * math.tau / 7 + 0.4 for k in range(7))]
EMBER["smoke"] = [((-0.06, 0.02, 0.18), (-0.05, 0.03, 0.35)), ((0.07, -0.03, 0.20), (0.06, 0.0, 0.42)),
                  ((0.0, 0.05, 0.22), (0.0, 0.04, 0.52))]


def build_ember_conjure():
    """A torso of flame rising out of a heap of embers, no legs: a core of
    black basalt plates cracked with orange light inside layered tongues of
    flame -- yellow at the heart, orange, red at the tips -- arms of flame
    ending in clawed hands of basalt, and a slab of a head with two
    white-yellow eyes in it."""
    r = new_rig()
    ember_heap(r, 0.36)
    names = []
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.32), "body")
    # A waist of fire out of the heap, narrower than the chest it carries, so
    # there is a figure and not a bonfire.
    r.add("column", C(0.15, 0.09, 0.24, squash_y=0.85), "pr_fire", "pelvis", loc=(0, 0, 0.06))
    for k in range(5):
        a = k * math.tau / 5 + 0.3
        x, y = math.sin(a), -math.cos(a)
        names.append(tongue(r, "fw%d" % k, "base", (x * 0.10, y * 0.08, 0.10),
                            (x * 0.15, y * 0.10 + 0.03, 0.44 + 0.06 * (k % 2)), 0.075))

    r.joint("chest", (0, 0, 0.16), "pelvis", rest=(8, 0, 0))
    # The body of fire the core sits in, and the tongues off it: up the back
    # and behind the shoulders, swept back, open at the front where the
    # basalt is.
    r.add("fire", E(0.25, 0.18, 0.27), "pr_fire_md", "chest", loc=(0, 0.05, 0.25))
    for k in range(7):
        a = math.radians(-105 + k * 35)
        x, y = math.sin(a), math.cos(a)
        tall = 0.66 - 0.12 * abs(k - 3) / 3.0
        names.append(tongue(r, "fb%d" % k, "chest", (x * 0.17, y * 0.10 + 0.07, 0.14),
                            (x * 0.26, y * 0.16 + 0.30, tall), 0.095))
    # The core: a breast of basalt plates -- two over the chest, two under
    # them, one low -- with the seams between them lit.
    # (Stood well proud of the magma: level with it, the lower plates sank
    # into the glow and the breast read as orange with a few smudges on it.)
    r.add("magma", E(0.20, 0.10, 0.24), "pr_magma_glow", "chest", loc=(0, -0.04, 0.24))
    for (x, z, rx, rz, tilt, seed) in ((-0.088, 0.37, 0.112, 0.088, 0.20, 1), (0.088, 0.37, 0.112, 0.088, -0.20, 2),
                                       (-0.08, 0.215, 0.10, 0.075, 0.10, 3), (0.08, 0.21, 0.10, 0.078, -0.12, 4),
                                       (0.0, 0.075, 0.11, 0.065, 0.0, 5)):
        r.add("plate", rock(rx, 0.07, rz, seed, 0.07), "pr_basalt" if seed % 2 else "pr_basalt_lt", "chest",
              loc=(x, -0.125 + abs(x) * 0.15, z), rot=(0.12, tilt, 0))
    crack(r, "chest", [(0.0, -0.19, 0.46), (0.012, -0.196, 0.36), (-0.008, -0.198, 0.29), (0.006, -0.195, 0.15),
                       (0.0, -0.18, 0.08)], 0.017)
    crack(r, "chest", [(-0.18, -0.14, 0.295), (-0.09, -0.185, 0.29), (0.0, -0.198, 0.292), (0.10, -0.185, 0.285),
                       (0.18, -0.14, 0.28)], 0.015)
    crack(r, "chest", [(-0.14, -0.15, 0.142), (-0.05, -0.18, 0.146), (0.06, -0.18, 0.138), (0.14, -0.15, 0.142)],
          0.014)
    # Shoulders in from the edge of the body and the arms flared only a
    # little: seen from the side, whatever is out to the side is drawn that
    # much higher or lower, and spread wider the far hand's claws came up
    # beside the face.
    for sx, side in ((-1, "l"), (1, "r")):
        names += flame_arm(r, side, sx, (sx * 0.26, 0.03, 0.40), 0.25, 0.23, 0.075, 9)

    r.joint("neck", (0, -0.03, 0.49), "chest")
    r.joint("head", (0, -0.01, 0.04), "neck", rest=(-18, 0, 0))
    # A slab of a head: a block of basalt with a ledge of brow, two white-hot
    # eyes under it set well apart, and a seam of fire for a mouth.
    r.add("skull", rock(0.145, 0.12, 0.125, 7, 0.06), "pr_basalt", "head", loc=(0, 0, 0.09))
    r.add("brow", rock(0.15, 0.055, 0.04, 8, 0.08), "pr_basalt_dk", "head", loc=(0, -0.095, 0.155))
    r.add("jaw", rock(0.10, 0.08, 0.05, 9, 0.10), "pr_basalt_dk", "head", loc=(0, -0.055, -0.005))
    for sx in (-1, 1):
        r.add("eye", E(0.046, 0.02, 0.034), "pr_fire_eye_glow", "head", loc=(sx * 0.072, -0.124, 0.092))
    crack(r, "head", [(-0.06, -0.125, 0.025), (-0.02, -0.132, 0.015), (0.025, -0.132, 0.022), (0.06, -0.125, 0.012)],
          0.013)
    # The crest: fire off the back of the head, swept back, clear of the face.
    for k in range(4):
        x = (k - 1.5) * 0.065
        names.append(tongue(r, "fh%d" % k, "head", (x, 0.05, 0.15),
                            (x * 1.5, 0.22, 0.40 - 0.06 * abs(k - 1.5)), 0.068))
    EMBER["tongues"][:] = names
    fire_bits(r, EMBER)
    return r.fit("ember_conjure", 48, ember_idle(0.0))


# The arms hang a little out from the body and a little forward of it, the
# claws hooked: held further forward, from the side they were a sleepwalker's.
EMBER_REST = {"shoulder_l": (-6, 0, 0), "shoulder_r": (-6, 0, 0), "elbow_l": X(-18), "elbow_r": X(-18),
              "hand_l": X(-14), "hand_r": X(-14)}
# Up and back, the fire flaring as it draws breath...
EMBER_WIND = {"chest": X(-16), "head": X(-8), "_y": 0.05, "_z": 0.04,
              "shoulder_l": (-150, 0, -24), "shoulder_r": (-150, 0, 24), "elbow_l": X(-40), "elbow_r": X(-40),
              "hand_l": X(-30), "hand_r": X(-30)}
# ...then both claws raked down through whatever is in front of it.
EMBER_RAKE = {"chest": X(26), "head": X(8), "_y": -0.18, "_z": -0.02,
              "shoulder_l": (-62, 0, 18), "shoulder_r": (-62, 0, -18), "elbow_l": X(-8), "elbow_r": X(-8),
              "hand_l": X(30), "hand_r": X(30)}


def fire_clips(info, rest, wind, blow, marks=(0.40, 0.58, 1.0), cape=False):
    """The five clips of a fire Conjure, from its rest, its wind-up and its
    blow: the fire on it never still (flicker), streaming back off it when
    it moves and flaring when it means to strike."""
    def shoulders(v, swing_):
        for side in ("l", "r"):
            x, y, z = v.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x + (swing_ if side == "l" else -swing_), y, z)
        return v

    def idle(t):
        s = sn(t)
        v = dict(rest)
        v.update({"_z": 0.012 * s, "chest": X(2 * s), "head": (0, 0, 5 * sn(t, 0.3))})
        shoulders(v, 5 * s)
        if cape:
            v["cape"] = X(4 * s)
        return flicker(v, info["tongues"], t)

    def walk(t):
        # A glide: no stride to take, so the body leans into the way it is
        # going, rides up and down over the heap, and the fire streams back.
        s = sn(t)
        v = dict(rest)
        v.update({"_z": 0.02 * abs(sn(t)), "chest": X(10 + 2 * s), "pelvis": (0, 0, 5 * s), "head": (0, 0, -4 * s)})
        shoulders(v, 16 * s)
        if cape:
            v["cape"] = (-18 - 5 * sn(t * 2), 0, 4 * s)
        return flicker(v, info["tongues"], t, amp=11.0, swell=0.20, speed=1, lean=(-16.0, 0.0))

    def attack(t):
        v = swing(t, rest, wind, blow, marks)
        i, k = phases(t, *marks)
        flare = [k, 1.0, 1.0 - k, 0.0][i]
        if cape:
            v["cape"] = X([-20 * k, -20 + 34 * k, 14 - 14 * k, 0][i])
        return flicker(v, info["tongues"], t, amp=10.0, swell=0.18, speed=2,
                       lean=(-20.0 * (1.0 if i == 1 else 0.0), 0.0), grow=0.35 * flare)

    def hurt(t):
        k = math.sin(t * math.pi)
        v = dict(rest)
        v.update(struck(k, shoulder_l=(-30, 0, -20), shoulder_r=(-30, 0, 20)))
        if cape:
            v["cape"] = X(-16 * k)
        return flicker(v, info["tongues"], t, amp=14.0, swell=0.2, speed=2, lean=(-18.0 * k, 0.0), grow=-0.30 * k)

    def death(t):
        # It flares, and the core bursts: plates of basalt thrown out of its
        # chest onto the ground, sparks up and out; the fire sinks back into
        # the heap and dies down, and last of all the coals go from orange to
        # black and a little smoke comes off them. (Frames fall at t = 0, .2,
        # .4, .6, .8, 1: flare, burst, sinking, nearly out, out but still
        # glowing, cold.)
        v = dict(rest)
        flare = ease(min(1.0, t / 0.2)) * (1.0 - ease(min(1.0, max(0.0, (t - 0.3) / 0.4))))
        sink = ease(min(1.0, max(0.0, (t - 0.25) / 0.55)))
        sh = rest.get("shoulder_l", (0, 0, 0))[0]
        el = rest.get("elbow_l", (0, 0, 0))[0]
        v.update({"chest": X(-12 * flare + 30 * sink), "head": X(-8 * flare + 20 * sink), "_z": 0.04 * flare,
                  "shoulder_l": (sh - 70 * flare - 40 * sink, 0, -30 * flare - 20 * sink),
                  "shoulder_r": (sh - 70 * flare - 40 * sink, 0, 30 * flare + 20 * sink),
                  "elbow_l": X(el - 30 * flare), "elbow_r": X(el - 30 * flare), "~body": -0.99 * sink})
        if cape:
            v["cape"] = X(-20 * flare)
        flicker(v, info["tongues"], t, amp=12.0, swell=0.2, speed=2, grow=0.45 * flare)
        gutter(v, info["tongues"], ease(min(1.0, max(0.0, (t - 0.3) / 0.5))))
        if t >= 0.35:
            b = (t - 0.35) * 2.0
            for i, (p0, vel) in enumerate(info["plates"]):
                p = thrown(p0, vel, b, grav=3.0, floor=0.04)
                put(v, "eplate", i, p - Vector(p0), 1.0, (160 * b * (i % 2 * 2 - 1), 110 * b, 0))
            for i, (p0, vel) in enumerate(info["sparks"]):
                p = thrown(p0, vel, b * 0.6, grav=2.0, floor=0.04)
                put(v, "espark", i, p - Vector(p0), max(0.0, 1.0 - b * 0.85))
        if t > 0.9:
            v["~hot"] = -1.0
            v["~cold"] = 0.0
            for i, (p0, d) in enumerate(info["smoke"]):
                put(v, "esmoke", i, d, 1.0)
        return v

    return idle, walk, attack, hurt, death


ember_idle, ember_walk, ember_attack, ember_hurt, ember_death = fire_clips(EMBER, EMBER_REST, EMBER_WIND, EMBER_RAKE)


# --- Inferno Conjure ------------------------------------------------------------------
INFERNO = {"tongues": []}
INFERNO["plates"] = burst([(-0.16, -0.14), (0.16, -0.14), (0.0, -0.18), (-0.14, 0.06), (0.14, 0.06), (0.0, 0.02),
                           (-0.24, -0.04), (0.24, -0.04), (-0.30, 0.0), (0.30, 0.0)], 0.86, 0.80, 0.60)
INFERNO["sparks"] = [((0.0, -0.06, 0.95), (math.sin(a) * 0.60, -math.cos(a) * 0.50, 1.7 + 0.4 * (k % 2)))
                     for k, a in enumerate(k * math.tau / 9 + 0.3 for k in range(9))]
INFERNO["smoke"] = [((-0.08, 0.02, 0.22), (-0.06, 0.04, 0.42)), ((0.09, -0.03, 0.24), (0.08, 0.0, 0.50)),
                    ((0.0, 0.06, 0.26), (0.0, 0.05, 0.62)), ((-0.02, -0.06, 0.24), (-0.03, -0.02, 0.30))]


def horn(r, name, parent, points, width, colour="pr_fire", edge="pr_flame_y_glow"):
    """A horn of fire: rigid, curved through `points` and tapering, solid
    enough to take the light -- a lit inner edge down it -- and a tongue of
    flame flickering at its point. Two tongues end to end, flickering each
    its own way, were two more flames in a crown of flames."""
    n = len(points) - 1
    for k, (a, b) in enumerate(zip(points, points[1:])):
        w0 = width * (1.0 - 0.8 * k / n)
        w1 = width * (1.0 - 0.8 * (k + 1) / n)
        r.limb("horn", a, b, w0, colour, parent, r_tip=max(0.006, w1))
        if k < n - 1:
            ea, eb = Vector(a), Vector(b)
            thin(r, "horn_edge", tuple(ea + Vector((0, -w0 * 0.55, 0))), tuple(eb + Vector((0, -w1 * 0.55, 0))),
                 w0 * 0.45, edge, parent, r_tip=max(0.004, w1 * 0.45))
    tip = Vector(points[-1])
    d = (tip - Vector(points[-2])).normalized()
    return [tongue(r, name, parent, tuple(tip - d * width * 0.3), tuple(tip + d * width * 2.4), width * 0.7)]


def build_inferno_conjure():
    """The Ember Conjure grown huge and older: the heap under it a hill, the
    fire up its back gone to a mantle down it, basalt pauldrons on the
    shoulders cracked with light, the chest a plate of molten rock glowing in
    a frame of basalt, a crown of horns of flame, and heavy fists of basalt
    with the fire wrapped round them."""
    r = new_rig()
    ember_heap(r, 0.42, seed=3, coals=15)
    names = []
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.36), "body")
    r.add("column", C(0.16, 0.10, 0.26, squash_y=0.85), "pr_fire", "pelvis", loc=(0, 0, 0.06))
    for k in range(6):
        a = k * math.tau / 6 + 0.3
        x, y = math.sin(a), -math.cos(a)
        names.append(tongue(r, "fw%d" % k, "base", (x * 0.11, y * 0.09, 0.12),
                            (x * 0.16, y * 0.11 + 0.03, 0.50 + 0.07 * (k % 2)), 0.085))

    r.joint("chest", (0, 0, 0.18), "pelvis", rest=(8, 0, 0))
    r.add("fire", E(0.30, 0.20, 0.30), "pr_fire_md", "chest", loc=(0, 0.06, 0.28))
    for k in range(5):
        a = math.radians(-70 + k * 35)
        x, y = math.sin(a), math.cos(a)
        names.append(tongue(r, "fb%d" % k, "chest", (x * 0.20, y * 0.12 + 0.10, 0.22),
                            (x * 0.30, y * 0.18 + 0.36, 0.68 - 0.10 * abs(k - 2) / 2.0), 0.11))
    # The mantle: a fall of fire from the shoulders down the back to the
    # heap, on a joint of its own so it swings, rows of flame licking up off
    # it -- bare, from behind it was one red egg.
    r.joint("cape", (0, 0.16, 0.52), "chest", rest=(10, 0, 0))
    r.add("mantle", C(0.30, 0.36, 0.60, squash_y=0.30), "pr_fire_dk", "cape", loc=(0, 0.02, 0.0))
    r.add("mantle_in", C(0.26, 0.30, 0.54, squash_y=0.22), "pr_fire", "cape", loc=(0, -0.02, -0.02))
    for row, (z0, h, w, n) in enumerate(((-0.08, 0.30, 0.24, 5), (-0.34, 0.28, 0.30, 5), (-0.60, 0.26, 0.34, 4))):
        for k in range(n):
            x = (k - (n - 1) / 2.0) / ((n - 1) / 2.0)
            names.append(tongue(r, "fm%d%d" % (row, k), "cape", (x * w, 0.12 + 0.02 * abs(x), z0),
                                (x * (w + 0.06), 0.26, z0 + h - 0.04 * abs(x)), 0.085))
    # The chest: a plate of molten rock in a frame of basalt, the crust on it
    # darker where it is cooling -- the Ember's breast the other way out.
    # (The crust runs in long seams: spots of it on the glow made a second
    # face, under the first.)
    r.add("molten", E(0.21, 0.10, 0.22), "pr_magma_glow", "chest", loc=(0, -0.10, 0.27))
    for (x, z, rx, rz, rot, seed) in ((0.0, 0.50, 0.25, 0.07, (0.3, 0, 0), 11),
                                      (-0.20, 0.30, 0.08, 0.20, (0.1, 0.2, 0), 12),
                                      (0.20, 0.30, 0.08, 0.20, (0.1, -0.2, 0), 13),
                                      (0.0, 0.08, 0.20, 0.07, (0, 0, 0), 14)):
        r.add("frame", rock(rx, 0.09, rz, seed, 0.08), "pr_basalt", "chest", loc=(x, -0.13 + abs(x) * 0.12, z), rot=rot)
    crack(r, "chest", [(-0.13, -0.19, 0.42), (-0.07, -0.21, 0.33), (-0.09, -0.21, 0.24), (-0.05, -0.20, 0.14)],
          0.015, "pr_basalt_dk")
    crack(r, "chest", [(0.12, -0.19, 0.40), (0.06, -0.21, 0.31), (0.09, -0.21, 0.21), (0.05, -0.20, 0.13)],
          0.015, "pr_basalt_dk")
    r.add("molten_hot", E(0.05, 0.03, 0.16), "pr_flame_y_glow", "chest", loc=(0, -0.195, 0.27))
    for sx, side in ((-1, "l"), (1, "r")):
        names += flame_arm(r, side, sx, (sx * 0.35, 0.03, 0.46), 0.32, 0.30, 0.10, 8, fist=True)
        # Pauldrons: a boulder of basalt on each shoulder, cracked with light,
        # fire coming up off the back of it.
        r.add("pauldron", rock(0.18, 0.16, 0.13, 20 + (sx > 0), 0.10), "pr_basalt", "shoulder_" + side,
              loc=(sx * 0.02, 0.0, 0.06))
        r.add("pauldron_lip", rock(0.16, 0.14, 0.05, 22 + (sx > 0), 0.10), "pr_basalt_lt", "shoulder_" + side,
              loc=(sx * 0.03, -0.01, -0.05))
        crack(r, "shoulder_" + side, [(sx * -0.06, -0.14, 0.12), (sx * 0.04, -0.16, 0.06), (sx * 0.10, -0.13, 0.10),
                                      (sx * 0.15, -0.09, 0.02)], 0.016)
        names.append(tongue(r, "fp" + side, "shoulder_" + side, (sx * 0.02, 0.08, 0.14), (sx * 0.10, 0.22, 0.42), 0.10))

    r.joint("neck", (0, -0.04, 0.56), "chest")
    r.joint("head", (0, -0.01, 0.04), "neck", rest=(-18, 0, 0))
    r.add("skull", rock(0.175, 0.145, 0.14, 15, 0.06), "pr_basalt", "head", loc=(0, 0, 0.10))
    r.add("brow", rock(0.19, 0.065, 0.048, 16, 0.08), "pr_basalt_dk", "head", loc=(0, -0.115, 0.175))
    r.add("jaw", rock(0.14, 0.11, 0.065, 17, 0.10), "pr_basalt_dk", "head", loc=(0, -0.06, -0.015))
    for sx in (-1, 1):
        r.add("eye", E(0.056, 0.022, 0.04), "pr_fire_eye_glow", "head", loc=(sx * 0.088, -0.146, 0.104))
    crack(r, "head", [(-0.09, -0.15, 0.025), (-0.03, -0.162, 0.010), (0.03, -0.162, 0.018), (0.09, -0.15, 0.006)],
          0.017)
    # The crown: horns of flame, thick at the root and curling out and up
    # like a bull's, a point of fire between them. Basalt at the roots, so a
    # horn starts somewhere.
    for sx in (-1, 1):
        r.add("horn_root", rock(0.07, 0.07, 0.06, 24 + (sx > 0)), "pr_basalt_dk", "head", loc=(sx * 0.15, 0.0, 0.18))
        names += horn(r, "fhorn%d" % (sx > 0), "head",
                      [(sx * 0.15, 0.0, 0.19), (sx * 0.30, 0.03, 0.24), (sx * 0.39, 0.06, 0.36),
                       (sx * 0.38, 0.08, 0.50), (sx * 0.32, 0.08, 0.60)], 0.075)
    names.append(tongue(r, "fc", "head", (0, 0.02, 0.19), (0, 0.12, 0.42), 0.07))
    INFERNO["tongues"][:] = names
    fire_bits(r, INFERNO, plate=0.085)
    return r.fit("inferno_conjure", 64, inferno_idle(0.0))


INFERNO_REST = {"shoulder_l": (-8, 0, 0), "shoulder_r": (-8, 0, 0), "elbow_l": X(-22), "elbow_r": X(-22),
                "hand_l": X(-6), "hand_r": X(-6)}
# Both fists raised over the crown, the fire on it flaring...
INFERNO_RAISE = {"chest": X(-18), "head": X(-8), "_y": 0.06, "_z": 0.03,
                 "shoulder_l": (-160, 0, -16), "shoulder_r": (-160, 0, 16), "elbow_l": X(-30), "elbow_r": X(-30),
                 "hand_l": X(-10), "hand_r": X(-10)}
# ...and brought down together on whatever is in front of it.
INFERNO_SLAM = {"chest": X(28), "head": X(-6), "_y": -0.16, "_z": -0.03,
                "shoulder_l": (-78, 0, 12), "shoulder_r": (-78, 0, -12), "elbow_l": X(-6), "elbow_r": X(-6),
                "hand_l": X(10), "hand_r": X(10)}

inferno_idle, inferno_walk, inferno_attack, inferno_hurt, inferno_death = fire_clips(
    INFERNO, INFERNO_REST, INFERNO_RAISE, INFERNO_SLAM, (0.44, 0.60, 1.0), cape=True)


# =================================================================================
#  Earth: the Stone Conjure and the Monolith Conjure
# =================================================================================

def _perp(d):
    """A direction square to d, leaning towards the camera side (-Y) and up."""
    p = Vector((0, -0.7, 0.7)).cross(d)
    if p.length < 1e-4:
        p = Vector((1, 0, 0)).cross(d)
    return d.cross(p).normalized()


def shard(r, parent, base, direction, length, width, colour, glint=None, root=None):
    """A crystal standing out of `base` along `direction`: a pointed prism,
    flat-faced so its facets take the light in bands, a lit glint down the
    face that looks up and out, and a lump of rock round its root."""
    d = Vector(direction).normalized()
    aim(r, r.add("shard", prism(width, length), colour, parent, loc=base), d)
    if glint:
        off = _perp(d) * width * 0.75
        b = Vector(base)
        thin(r, "glint", tuple(b + off + d * length * 0.12), tuple(b + off * 0.5 + d * length * 0.62), width * 0.30,
             glint, parent, r_tip=0.004)
    if root:
        r.add("shard_root", rock(width * 1.3, width * 1.3, width * 0.9, int(abs(base[0] * 997 + base[2] * 131)) % 50),
              root, parent, loc=base)


XTAL_G = ("pr_xtal_g", "pr_xtal_g_glow")
XTAL_A = ("pr_xtal_a", "pr_xtal_a_glow")


def cluster(r, parent, base, normal, sizes, width, kinds, spread=24, turn=0.0, root="pr_stone_dk"):
    """Shards grown out of one place, fanned round `normal`, longest first,
    the colours taking turns."""
    n = Vector(normal).normalized()
    a = _perp(n)
    b = n.cross(a).normalized()
    for k, length in enumerate(sizes):
        if k == 0:
            d = n
        else:
            ang = turn + (k - 1) * math.tau / max(1, len(sizes) - 1)
            s = math.radians(spread)
            d = (n * math.cos(s) + (a * math.cos(ang) + b * math.sin(ang)) * math.sin(s)).normalized()
        col, glint = kinds[k % len(kinds)]
        shard(r, parent, tuple(Vector(base) + (d - n) * width * 0.7), d, length, width * (1.0 if k == 0 else 0.8),
              col, glint, root if k == 0 else None)


def rubble(r, radius, seed, n=11, col=("pr_stone", "pr_stone_dk", "pr_stone_lt"), height=0.5, blocky=False):
    """The heap a stone Conjure falls to, hidden until it does: rocks piled in
    a low mound on a joint of its own -- or, for cut stone, broken blocks."""
    r.joint("rubble", (0, 0, 0), "base")
    rnd = random.Random(seed)
    r.add("heap", rock(radius * 0.85, radius * 0.80, radius * 0.30 * height * 2, seed, 0.14), col[1], "rubble",
          loc=(0, 0, radius * 0.08))
    for k in range(n):
        a = k * math.tau / n + rnd.uniform(-0.3, 0.3)
        d = radius * rnd.uniform(0.15, 0.85)
        s = radius * rnd.uniform(0.20, 0.34)
        z = radius * 0.30 * height * 2 * max(0.0, 1 - (d / radius) ** 2) ** 0.5 + s * 0.3
        mesh = (slab(s * 1.7, s * 1.3, s * 1.0, s * 0.12) if blocky else rock(s, s * 0.9, s * 0.75, seed * 17 + k, 0.2))
        r.add("stone", mesh, col[k % 3], "rubble", loc=(math.cos(a) * d, math.sin(a) * d, z),
              rot=(rnd.uniform(-0.4, 0.4), rnd.uniform(-0.4, 0.4), rnd.uniform(0, math.pi)))
    r.j["rubble"].scale = (0.01, 0.01, 0.01)


def earth_bits(r, info, size):
    """Stones that break off a falling stone Conjure -- boulders, or for cut
    stone broken blocks -- and a crystal or two."""
    stones = Bits(r, "estone")
    cols = ("pr_mono", "pr_mono_dk", "pr_mono_lt") if info.get("dark") else ("pr_stone", "pr_stone_dk", "pr_stone_lt")
    for i, (p0, _) in enumerate(info["stones"]):
        s = size * (1.0 + 0.25 * (i % 3))
        if info.get("dark"):
            mesh = slab(s * 1.6, s * 1.2, s * 1.0, s * 0.12)
        else:
            mesh = rock(s, s * 0.9, s * 0.8, 70 + i, 0.18)
        stones.add(p0, mesh, cols[i % 3], rot=(0.3 * (i % 3), 0.5 * (i % 2), 0.7 * i))
    shards = Bits(r, "eshard")
    for i, (p0, _) in enumerate(info["shards"]):
        shards.add(p0, prism(size * 0.45, size * 2.2), ("pr_xtal_g", "pr_xtal_a")[i % 2], rot=(1.2, 0.4 * i, 0))


def mortar(r, parent, loc, rx, ry, rz):
    """Amber light in the joint between two stones: what holds it together."""
    r.add("mortar", E(rx, ry, rz), "pr_amber_glow", parent, loc=loc)


def build_stone_conjure():
    """A hulking figure of stacked boulders held together by glowing amber
    veins: short thick legs of stacked stones, a hunched chest, long arms
    ending in fists of rock, shards of pale green and amber crystal jutting
    from its shoulders, and a slab of a head set low on the front of its
    chest, with eyes of amber light in it."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    rubble(r, 0.40, 5)
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.44), "body")
    r.add("hips", rock(0.24, 0.17, 0.12, 30, 0.12), "pr_stone_dk", "pelvis", loc=(0, 0.01, 0.0))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.15, 0, -0.05), "pelvis")
        r.add("thigh", rock(0.115, 0.115, 0.11, 31 + (sx > 0), 0.14), "pr_stone", "hip_" + side, loc=(0, 0, -0.08))
        r.joint("knee_" + side, (0, 0, -0.20), "hip_" + side)
        mortar(r, "knee_" + side, (0, -0.01, 0.0), 0.075, 0.075, 0.035)
        r.add("shin", rock(0.11, 0.11, 0.10, 33 + (sx > 0), 0.14), "pr_stone_lt", "knee_" + side, loc=(0, -0.01, -0.08))
        r.add("foot", rock(0.12, 0.16, 0.05, 35 + (sx > 0), 0.10), "pr_stone_dk", "knee_" + side, loc=(0, -0.04, -0.16))
        mortar(r, "knee_" + side, (0, -0.02, -0.145), 0.06, 0.07, 0.025)
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(16, 0, 0))
    r.add("belly", rock(0.22, 0.17, 0.14, 36, 0.10), "pr_stone", "chest", loc=(0, 0, 0.10))
    mortar(r, "chest", (0, -0.01, 0.235), 0.20, 0.14, 0.04)
    r.add("chest_stone", rock(0.35, 0.25, 0.22, 37, 0.10), "pr_stone", "chest", loc=(0, 0.02, 0.40))
    r.add("hump", rock(0.25, 0.17, 0.15, 38, 0.12), "pr_stone_dk", "chest", loc=(0, 0.15, 0.52))
    # The veins: amber light run through the stones in branching seams.
    crack(r, "chest", [(-0.24, -0.20, 0.46), (-0.12, -0.235, 0.40), (-0.02, -0.245, 0.43), (0.06, -0.24, 0.36),
                       (0.18, -0.215, 0.39), (0.27, -0.17, 0.33)], 0.018, "pr_amber_glow")
    crack(r, "chest", [(-0.02, -0.245, 0.43), (0.0, -0.24, 0.52), (-0.06, -0.21, 0.58)], 0.016, "pr_amber_glow")
    crack(r, "chest", [(-0.12, -0.16, 0.12), (-0.03, -0.18, 0.07), (0.06, -0.175, 0.13), (0.14, -0.15, 0.08)],
          0.015, "pr_amber_glow")
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.40, 0.03, 0.47), "chest", rest=(0, -sx * 8, 0))
        r.add("shoulder_stone", rock(0.17, 0.16, 0.15, 40 + (sx > 0), 0.10), "pr_stone_lt", "shoulder_" + side,
              loc=(sx * 0.02, 0, 0.02))
        r.add("upper", rock(0.11, 0.11, 0.14, 42 + (sx > 0), 0.14), "pr_stone", "shoulder_" + side, loc=(0, 0, -0.17))
        r.joint("elbow_" + side, (0, 0, -0.30), "shoulder_" + side, rest=(-14, 0, 0))
        mortar(r, "elbow_" + side, (0, 0, 0.0), 0.07, 0.07, 0.035)
        r.add("fore", rock(0.115, 0.11, 0.14, 44 + (sx > 0), 0.14), "pr_stone_dk", "elbow_" + side, loc=(0, 0, -0.14))
        r.joint("hand_" + side, (0, 0, -0.28), "elbow_" + side)
        mortar(r, "hand_" + side, (0, 0, 0.02), 0.065, 0.065, 0.03)
        r.add("fist", rock(0.15, 0.14, 0.14, 46 + (sx > 0), 0.12), "pr_stone", "hand_" + side, loc=(0, -0.02, -0.09))
        crack(r, "shoulder_" + side, [(sx * -0.04, -0.15, 0.06), (sx * 0.05, -0.16, 0.0), (sx * 0.13, -0.12, 0.05)],
              0.015, "pr_amber_glow")
        # The crystals: a cluster out of the top of each shoulder, splayed.
        cluster(r, "shoulder_" + side, (sx * 0.05, 0.04, 0.12), (sx * 0.45, 0.25, 0.85), (0.26, 0.18, 0.15, 0.13),
                0.055, (XTAL_G, XTAL_A) if sx < 0 else (XTAL_A, XTAL_G), spread=30, turn=1.0 + sx)
    # The head sits up on the front of the chest stone, not in it: set any
    # lower, from the front its eyes were a pair of lights on its chest.
    r.joint("neck", (0, -0.15, 0.60), "chest", rest=(-16, 0, 0))
    r.joint("head", (0, -0.02, 0.04), "neck", rest=(-16, 0, 0))
    r.add("slab", rock(0.19, 0.15, 0.115, 48, 0.08), "pr_stone_lt", "head", loc=(0, 0, 0.08))
    r.add("brow", rock(0.19, 0.06, 0.04, 49, 0.10), "pr_stone_dk", "head", loc=(0, -0.125, 0.13))
    r.add("jaw", rock(0.14, 0.10, 0.055, 50, 0.12), "pr_stone_dk", "head", loc=(0, -0.05, -0.03))
    for sx in (-1, 1):
        r.add("eye", E(0.05, 0.02, 0.032), "pr_amber_eye_glow", "head", loc=(sx * 0.082, -0.148, 0.08))
    shard(r, "head", (0.08, 0.02, 0.16), (0.3, 0.3, 0.9), 0.14, 0.042, "pr_xtal_a", "pr_xtal_a_glow")
    earth_bits(r, STONE, 0.07)
    return r.fit("stone_conjure", 52, stone_idle(0.0))


STONE = {"stones": burst([(-0.22, -0.10), (0.22, -0.10), (0.0, -0.18), (-0.30, 0.04), (0.30, 0.04), (0.0, 0.12),
                          (-0.12, -0.16), (0.12, -0.16), (-0.18, 0.10), (0.18, 0.10)], 0.62, 0.32, 0.25),
         # (Thrown short, and as much back or forward as out: thrown out to
         # the side, in the side rows a shard came at the camera and landed on
         # the bottom edge of the frame.)
         "shards": [((-0.36, 0.06, 0.92), (-0.20, 0.18, 0.3)), ((0.36, 0.06, 0.92), (0.20, -0.16, 0.3))]}
STONE_REST = {"shoulder_l": (-14, 0, 4), "shoulder_r": (-14, 0, -4), "elbow_l": X(-16), "elbow_r": X(-16),
              "hand_l": X(-8), "hand_r": X(-8)}
# Both fists up over its head, rearing back...
STONE_RAISE = {"chest": X(-20), "neck": X(-6), "head": X(-4), "_y": 0.05, "_z": 0.02,
               "shoulder_l": (-165, 0, 18), "shoulder_r": (-165, 0, -18), "elbow_l": X(-30), "elbow_r": X(-30),
               "hip_l": fwd(-6), "hip_r": fwd(-6)}
# ...and both brought down together on whatever is in front of it. (Down in
# front of its own feet: swung on out to the level, from the side its fists
# went through the edge of the frame.)
STONE_SMASH = {"chest": X(20), "neck": X(4), "head": X(-8), "_y": -0.06, "_z": -0.05,
               "shoulder_l": (-72, 0, 12), "shoulder_r": (-72, 0, -12), "elbow_l": X(-10), "elbow_r": X(-10),
               "hand_l": X(-16), "hand_r": X(-16),
               "hip_l": fwd(26), "hip_r": fwd(-14), "knee_l": X(24), "knee_r": X(18)}


def earth_clips(info, rest, wind, blow, marks=(0.44, 0.60, 1.0), stride=24, arms=18):
    """The five clips of a stone Conjure: a heavy rolling tread on its stone
    legs, a two-fisted smash, and a death that slumps and comes apart into
    rubble -- the light going out of its veins as the stones part."""
    def idle(t):
        s = sn(t)
        v = dict(rest)
        v.update({"_z": 0.008 * s, "chest": X(2.5 * s), "head": (0, 0, 5 * sn(t, 0.3)), "neck": X(-2 * s)})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x + 3 * s * k, y, z)
        return v

    def walk(t):
        v = gait(t, stride, stride * 0.9, 0, 0.035, 6)
        v.update(rest)
        s = sn(t)
        v.update({"pelvis": (0, 6 * s, 0), "chest": (4, 0, 6 * s), "head": (0, 0, -6 * s),
                  "_z": 0.03 * abs(s) - 0.012})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x - arms * s * k, y, z)
        return v

    def attack(t):
        return swing(t, rest, wind, blow, marks)

    def hurt(t):
        v = dict(rest)
        v.update(struck(math.sin(t * math.pi) * 0.8, shoulder_l=(-30, 0, 20), shoulder_r=(-30, 0, -20)))
        return v

    def death(t):
        # It slumps -- knees gone, the chest down over them -- and comes
        # apart: stones break off it and fall, and what is left sinks into a
        # heap of rubble that rises as it goes. (Frames: standing, slumped,
        # breaking, half down, the last of it, rubble.)
        slump = ease(min(1.0, t / 0.25))
        fall = ease(min(1.0, max(0.0, (t - 0.2) / 0.65)))
        v = dict(rest)
        v.update({"hip_l": fwd(40 * slump), "hip_r": fwd(30 * slump), "knee_l": X(60 * slump), "knee_r": X(50 * slump),
                  "chest": X(22 * slump), "neck": X(10 * slump), "head": X(14 * slump),
                  "_z": -0.10 * slump, "~body": -0.99 * fall,
                  "~rubble": -0.99 + 0.99 * ease(min(1.0, max(0.0, (t - 0.25) / 0.6)))})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x - 30 * slump, y, z + 14 * slump * k)
        if t >= 0.35:
            b = (t - 0.35) * 2.0
            for i, (p0, vel) in enumerate(info["stones"]):
                p = thrown(p0, vel, b, grav=3.2, floor=0.05)
                put(v, "estone", i, p - Vector(p0), 1.0, (90 * b * (i % 2 * 2 - 1), 70 * b, 0))
            for i, (p0, vel) in enumerate(info["shards"]):
                p = thrown(p0, vel, b, grav=3.2, floor=0.03)
                put(v, "eshard", i, p - Vector(p0), 1.0, (0, 0, 40 * b))
        return v

    return idle, walk, attack, hurt, death


stone_idle, stone_walk, stone_attack, stone_hurt, stone_death = earth_clips(STONE, STONE_REST, STONE_RAISE, STONE_SMASH)


# --- Monolith Conjure -------------------------------------------------------------------
def rune(r, parent, centre, size, glyph, y_face, colour="pr_amber_glow", width=0.018):
    """A rune cut into a face of stone and lit from inside: strokes of a glyph
    drawn on a unit square (x across, z up) at `centre`, a hair proud of the
    face at depth y_face."""
    cx, cz = centre
    for stroke in glyph:
        pts = [(cx + x * size, y_face, cz + z * size) for (x, z) in stroke]
        crack(r, parent, pts, width, colour)


# Glyphs on a unit square, centre (0, 0): strokes of points.
GLYPHS = {
    "branch": [[(0, -0.5), (0, 0.5)], [(0, 0.1), (-0.35, 0.45)], [(0, -0.15), (0.35, 0.2)]],
    "eye": [[(-0.45, 0), (0, 0.4), (0.45, 0), (0, -0.4), (-0.45, 0)], [(0, -0.12), (0, 0.12)]],
    "chevron": [[(-0.4, 0.35), (0, -0.05), (0.4, 0.35)], [(-0.4, -0.05), (0, -0.45), (0.4, -0.05)]],
    "fork": [[(0, -0.5), (0, 0.05)], [(-0.35, 0.5), (0, 0.05), (0.35, 0.5)]],
    "bar": [[(-0.4, 0.3), (0.4, 0.3)], [(0, 0.3), (0, -0.45)], [(-0.3, -0.45), (0.3, -0.45)]],
}


def moss(r, parent, loc, rx, ry, rz=0.025, seed=0):
    r.add("moss", rock(rx, ry, rz, 90 + seed, 0.2), "pr_moss" if seed % 2 else "pr_moss_dk", parent, loc=loc)


def roots(r, parent, start, drop, seed=0):
    """A root out of a crack, hanging and kinking as it goes."""
    rnd = random.Random(seed)
    p = Vector(start)
    for k in range(3):
        q = p + Vector((rnd.uniform(-0.04, 0.04), rnd.uniform(-0.02, 0.02), -drop / 3.0))
        thin(r, "root", tuple(p), tuple(q), 0.016 - 0.003 * k, "pr_root", parent, r_tip=0.012 - 0.003 * k)
        p = q


def build_monolith_conjure():
    """The Stone Conjure grown huge, older and squarer: a torso that is one
    standing slab of dark stone carved with runes lit amber from inside, arms
    like pillars ending in boulder fists, legs of squared stone, a head that
    is a block set on the slab with a crown of crystals growing out of it,
    and moss on its shoulders and roots hanging out of its cracks."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    rubble(r, 0.52, 8, n=13, col=("pr_mono", "pr_mono_dk", "pr_mono_lt"), blocky=True)
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.44), "body")
    r.add("hips", slab(0.50, 0.32, 0.16, 0.03), "pr_mono_dk", "pelvis", loc=(0, 0.0, 0.0))
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("hip_" + side, (sx * 0.16, 0, -0.06), "pelvis")
        r.add("thigh", slab(0.20, 0.20, 0.20, 0.03), "pr_mono", "hip_" + side, loc=(0, 0, -0.08))
        r.joint("knee_" + side, (0, 0, -0.20), "hip_" + side)
        mortar(r, "knee_" + side, (0, -0.01, 0.0), 0.08, 0.08, 0.03)
        r.add("shin", slab(0.19, 0.19, 0.15, 0.03), "pr_mono_lt", "knee_" + side, loc=(0, -0.01, -0.07))
        r.add("foot", slab(0.23, 0.30, 0.07, 0.025), "pr_mono_dk", "knee_" + side, loc=(0, -0.05, -0.15))
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(6, 0, 0))
    # The slab, and what is written on it.
    r.add("torso", slab(0.74, 0.42, 0.80, 0.045), "pr_mono", "chest", loc=(0, 0, 0.44))
    r.add("plinth", slab(0.60, 0.36, 0.10, 0.03), "pr_mono_dk", "chest", loc=(0, 0, 0.02))
    front, back = -0.214, 0.214
    rune(r, "chest", (0.0, 0.60), 0.20, GLYPHS["eye"], front)
    rune(r, "chest", (-0.20, 0.30), 0.17, GLYPHS["branch"], front)
    rune(r, "chest", (0.20, 0.30), 0.17, GLYPHS["fork"], front)
    rune(r, "chest", (0.0, 0.25), 0.15, GLYPHS["bar"], front)
    rune(r, "chest", (-0.18, 0.55), 0.17, GLYPHS["chevron"], back)
    rune(r, "chest", (0.18, 0.40), 0.17, GLYPHS["branch"], back)
    # Cracks with moss in them, roots out of them, and moss along the top.
    crack(r, "chest", [(0.37, -0.12, 0.72), (0.30, -0.216, 0.62), (0.33, -0.216, 0.50)], 0.012, "pr_mono_dk")
    crack(r, "chest", [(-0.37, -0.10, 0.18), (-0.31, -0.216, 0.12), (-0.28, -0.216, 0.02)], 0.012, "pr_mono_dk")
    for (x, y, z, rx, ry, k) in ((-0.20, -0.05, 0.85, 0.18, 0.13, 1), (0.16, 0.06, 0.85, 0.16, 0.14, 2),
                                 (0.32, -0.215, 0.60, 0.05, 0.02, 3), (-0.30, -0.21, 0.10, 0.06, 0.02, 4)):
        moss(r, "chest", (x, y, z), rx, ry, 0.03 if z > 0.8 else 0.05, k)
    roots(r, "chest", (0.33, -0.22, 0.52), 0.30, 1)
    roots(r, "chest", (-0.37, 0.10, 0.70), 0.36, 2)
    roots(r, "chest", (0.37, 0.12, 0.40), 0.28, 3)
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.50, 0.0, 0.70), "chest", rest=(0, -sx * 6, 0))
        r.add("cap", slab(0.28, 0.30, 0.14, 0.035), "pr_mono_lt", "shoulder_" + side, loc=(sx * 0.02, 0, 0.04))
        moss(r, "shoulder_" + side, (sx * 0.01, 0.02, 0.115), 0.12, 0.12, 0.03, 5 + (sx > 0))
        r.add("upper", slab(0.20, 0.20, 0.30, 0.03), "pr_mono", "shoulder_" + side, loc=(0, 0, -0.20))
        r.joint("elbow_" + side, (0, 0, -0.38), "shoulder_" + side, rest=(-10, 0, 0))
        mortar(r, "elbow_" + side, (0, 0, 0.01), 0.08, 0.08, 0.03)
        r.add("fore", slab(0.19, 0.19, 0.28, 0.03), "pr_mono_dk", "elbow_" + side, loc=(0, 0, -0.15))
        rune(r, "elbow_" + side, (0.0, -0.15), 0.12, GLYPHS["chevron"], -0.097, width=0.015)
        r.joint("hand_" + side, (0, 0, -0.32), "elbow_" + side)
        r.add("fist", rock(0.20, 0.19, 0.19, 52 + (sx > 0), 0.12), "pr_mono_lt", "hand_" + side, loc=(0, -0.02, -0.11))
        roots(r, "shoulder_" + side, (sx * 0.10, 0.10, -0.08), 0.22, 4 + (sx > 0))
    r.joint("head", (0, -0.05, 0.84), "chest", rest=(-14, 0, 0))
    r.add("block", slab(0.38, 0.30, 0.28, 0.04), "pr_mono_lt", "head", loc=(0, 0, 0.15))
    r.add("brow", slab(0.40, 0.08, 0.065, 0.02), "pr_mono_dk", "head", loc=(0, -0.145, 0.22))
    for sx in (-1, 1):
        r.add("eye", E(0.058, 0.02, 0.036), "pr_amber_eye_glow", "head", loc=(sx * 0.095, -0.156, 0.15))
    crack(r, "head", [(-0.09, -0.156, 0.07), (0.09, -0.156, 0.07)], 0.015, "pr_amber_glow")
    # The crown: crystals grown out of the top of the block in a ring.
    for k, (x, y, lx, ly, length) in enumerate(((-0.13, -0.07, -0.35, -0.15, 0.24), (0.13, -0.07, 0.35, -0.15, 0.26),
                                                (0.0, -0.09, 0.0, -0.2, 0.30), (-0.11, 0.08, -0.30, 0.35, 0.22),
                                                (0.11, 0.08, 0.30, 0.35, 0.20), (0.0, 0.09, 0.0, 0.30, 0.26))):
        col, glint = (XTAL_G, XTAL_A)[k % 2]
        shard(r, "head", (x, y, 0.29), (lx, ly, 1.0), length, 0.058, col, glint, "pr_mono_dk" if k < 3 else None)
    earth_bits(r, MONOLITH, 0.09)
    return r.fit("monolith_conjure", 68, monolith_idle(0.0))


MONOLITH = {"stones": burst([(-0.30, -0.12), (0.30, -0.12), (0.0, -0.20), (-0.36, 0.06), (0.36, 0.06), (0.0, 0.16),
                             (-0.16, -0.18), (0.16, -0.18), (-0.24, 0.14), (0.24, 0.14), (-0.40, -0.04),
                             (0.40, -0.04)], 0.80, 0.50, 0.25),
            "shards": [((-0.12, -0.10, 1.25), (-0.40, -0.10, 0.3)), ((0.12, -0.10, 1.25), (0.45, -0.05, 0.3)),
                       ((0.0, 0.08, 1.30), (0.05, 0.40, 0.4))],
            "dark": True}
MONOLITH_REST = {"shoulder_l": (-8, 0, 3), "shoulder_r": (-8, 0, -3), "elbow_l": X(-12), "elbow_r": X(-12),
                 "hand_l": X(-6), "hand_r": X(-6)}
# Both pillars up, and down on the ground in front of it.
MONOLITH_RAISE = {"chest": X(-14), "head": X(-6), "_y": 0.05, "_z": 0.02,
                  "shoulder_l": (-160, 0, 10), "shoulder_r": (-160, 0, -10), "elbow_l": X(-24), "elbow_r": X(-24),
                  "hip_l": fwd(-6), "hip_r": fwd(-6)}
MONOLITH_POUND = {"chest": X(22), "head": X(-8), "_y": -0.12, "_z": -0.05,
                  "shoulder_l": (-86, 0, 8), "shoulder_r": (-86, 0, -8), "elbow_l": X(-4), "elbow_r": X(-4),
                  "hand_l": X(-10), "hand_r": X(-10),
                  "hip_l": fwd(26), "hip_r": fwd(-14), "knee_l": X(20), "knee_r": X(16)}

monolith_idle, monolith_walk, monolith_attack, monolith_hurt, monolith_death = earth_clips(
    MONOLITH, MONOLITH_REST, MONOLITH_RAISE, MONOLITH_POUND, (0.46, 0.62, 1.0), stride=20, arms=12)


# =================================================================================
#  Water: the Tide Conjure and the Maelstrom Conjure
# =================================================================================

def chain(r, parent, points, w0, w1, colour, name="ribbon"):
    """A ribbon of water or air: one tube through `points`, tapering from w0
    to w1."""
    n = len(points) - 1
    tube(r, name, points, [w0 + (w1 - w0) * k / float(n) for k in range(n + 1)], colour, parent)


def curl_points(base, u, v, r0, r1, sweep=300.0, n=9):
    """A breaking wave's curl, from `base`: up along v, over towards u and
    down, winding in as it goes (the radius running r0 to r1). u and v are
    the curl's forward and up in the parent's space."""
    base, u, v = Vector(base), Vector(u).normalized(), Vector(v).normalized()
    pts = []
    for k in range(n + 1):
        f = k / n
        a = math.radians(180.0 - sweep * f)        # from the left of the circle, over the top, round
        rad = r0 + (r1 - r0) * f
        centre = base + u * r0
        pts.append(centre + (u * math.cos(a) + v * math.sin(a)) * rad)
    return pts


def sheet_curl(r, parent, base, u, v, r0, r1, half, thick, sweep=230.0, lip=0.62, n=9, across=(1, 0, 0),
               water="pr_water_lt", foam="pr_foam"):
    """A curl swept sideways into a sheet: along the curl's path a bar laid
    across it (`across`), `half` either side, narrowing towards the lip. Seen
    edge-on a row of curls is a comb; a sheet is a wave from every side."""
    pts = curl_points(base, u, v, r0, r1, sweep, n)
    ax = Vector(across).normalized()
    m = int(round(n * (1.0 - lip)))
    for k, p in enumerate(pts):
        f = k / float(n)
        hw = half * (1.0 - 0.45 * f)
        th = thick * (1.0 - 0.55 * f)
        thin(r, "wave", tuple(p - ax * hw), tuple(p + ax * hw), th, water if k < m else foam, parent, r_tip=th)
    return pts


def sheet(r, parent, pts, half, thick, lip=0.6, across=(1, 0, 0), water="pr_water_lt", foam="pr_foam", taper=0.45):
    """A sheet of water swept along `pts`: a bar laid across the path at
    every point, `half` either side, the last `lip` of it foam."""
    ax = Vector(across).normalized()
    n = len(pts) - 1
    m = int(round(n * (1.0 - lip)))
    for k, p in enumerate(pts):
        f = k / float(n)
        hw = half * (1.0 - taper * f)
        th = thick * (1.0 - 0.5 * f)
        p = Vector(p)
        thin(r, "wave", tuple(p - ax * hw), tuple(p + ax * hw), th, water if k < m else foam, parent, r_tip=th)


# The crest's path, for a head of radius about 0.13 centred 0.10 up its joint:
# up the back of the head, over the top and breaking forward -- the lip
# coming down no further than the top of the brow. Wound round the head the
# way a real wave curls, the camera, looking down, saw the lip and nothing of
# the face under it.
CREST = [(0, 0.13, 0.04), (0, 0.15, 0.15), (0, 0.13, 0.25), (0, 0.07, 0.32), (0, -0.01, 0.345), (0, -0.075, 0.32),
         (0, -0.11, 0.27), (0, -0.10, 0.225), (0, -0.065, 0.215)]


def crest(r, parent, s, half, thick, lip=0.38):
    """A breaking wave over a head, `s` times the size it is drawn for:
    water rising up the back of the head, over the top and breaking forward
    over the brow, white along its lip, and spray thrown up off the top."""
    # (White only along the lip: white over the whole top, from the front
    # it was a white block where the head should be.)
    sheet(r, parent, [Vector(p) * s for p in CREST], half, thick, lip=lip)
    for k, (x, y, z) in enumerate(((-0.06, -0.02, 0.40), (0.05, 0.03, 0.42), (0.10, -0.06, 0.37), (-0.11, 0.02, 0.36))):
        r.add("spray", E(0.02 * s, 0.02 * s, 0.02 * s), "pr_foam", parent, loc=(x * s, y * s, z * s))


def helix(z0, z1, r0, r1, turns, phase, n):
    """Points up a helix: radius r0 at z0 to r1 at z1, `turns` times round."""
    return [Vector(((r0 + (r1 - r0) * f) * math.cos(phase + turns * math.tau * f),
                    (r0 + (r1 - r0) * f) * math.sin(phase + turns * math.tau * f), z0 + (z1 - z0) * f))
            for f in (k / float(n) for k in range(n + 1))]


def swirl_column(r, z0, z1, r0, r1, bands, w, parent="base", name="swirl", core=("pr_water", 0.85)):
    """The column of water a water Conjure has for legs: a core tapering down
    to the ground and bands of lighter water and foam wound round it, on a
    joint of its own that a pose turns -- round and round, so the bands climb
    like a barber's pole."""
    r.joint(name, (0, 0, 0), parent)
    r.add("core", C(r1 * core[1], r0 * core[1], z1 - z0), core[0], name, loc=(0, 0, z1))
    for k, col in enumerate(bands):
        chain(r, name, helix(z0, z1, r0, r1, 1.1, k * math.tau / len(bands), 10), w, w * 0.8, col)


def pool(r, radius, name="pool", parent="base", foam=True):
    """Water standing on the ground round the foot of the column."""
    r.joint(name, (0, 0, 0), parent)
    r.add("pool", E(radius, radius * 0.92, 0.022), "pr_water_dk", name, loc=(0, 0, 0.012))
    r.add("pool_lt", E(radius * 0.72, radius * 0.66, 0.024), "pr_water", name,
          loc=(-radius * 0.08, -radius * 0.06, 0.016))
    if foam:
        r.add("pool_foam", TORUS(radius * 0.88, 0.022), "pr_foam", name, loc=(0, 0, 0.02))


def puddle(r, radius):
    """The puddle a water Conjure leaves: hidden until its death spreads it."""
    r.joint("puddle", (0, 0, 0), "base")
    r.add("puddle", E(radius, radius * 0.82, 0.02), "pr_water_dk", "puddle", loc=(0, 0, 0.01))
    r.add("puddle_lt", E(radius * 0.70, radius * 0.56, 0.022), "pr_water", "puddle",
          loc=(-radius * 0.1, -radius * 0.05, 0.014))
    r.add("puddle_foam", TORUS(radius * 0.86, 0.02), "pr_foam", "puddle", loc=(0, 0, 0.02))
    for k in range(5):
        a = k * math.tau / 5 + 0.4
        r.add("puddle_fleck", E(0.03, 0.022, 0.012), "pr_foam", "puddle",
              loc=(math.cos(a) * radius * 0.45, math.sin(a) * radius * 0.40, 0.03))
    r.j["puddle"].scale = (0.01, 0.01, 0.01)


def water_arm(r, side, sx, sh, upper, fore, w, flare, colour="pr_water"):
    """An arm of water ending in a wave: the forearm runs on into a curl
    that breaks forward, white at the lip."""
    r.joint("shoulder_" + side, sh, "chest", rest=(0, -sx * flare, 0))
    r.limb("upper", (0, 0, 0), (0, 0, -upper), w, colour, "shoulder_" + side, r_tip=w * 0.9)
    r.joint("elbow_" + side, (0, 0, -upper), "shoulder_" + side, rest=(-14, 0, 0))
    r.limb("fore", (0, 0, 0), (0, 0, -fore), w * 0.92, colour, "elbow_" + side, r_tip=w * 0.95)
    r.joint("hand_" + side, (0, 0, -fore), "elbow_" + side)
    # The wave the hand is: on down the arm, forward and over, and back up.
    sheet_curl(r, "hand_" + side, (0, w * 0.2, w * 0.3), (0, -1, 0), (0, 0, -1), w * 2.1, w * 0.7, w * 0.75,
               w * 0.75, sweep=290, lip=0.55, n=8)


TIDE = {}
TIDE["drops"] = burst([(-0.12, -0.10), (0.12, -0.10), (0.0, -0.16), (-0.16, 0.04), (0.16, 0.04), (0.0, 0.10),
                       (-0.06, -0.14), (0.06, -0.14), (-0.10, 0.10), (0.10, 0.10)], 0.62, 0.62, 1.2)


def water_bits(r, info, size):
    drops = Bits(r, "edrop")
    for i, (p0, _) in enumerate(info["drops"]):
        s = size * (1.0 + 0.3 * (i % 3))
        drops.add(p0, E(s, s, s * 1.25), ("pr_water_lt", "pr_foam", "pr_water")[i % 3])


def build_tide_conjure():
    """A figure of water: a deep-blue body with lighter water layered over it,
    a crest of white foam curling over its head like a breaking wave; below
    the waist, instead of legs, a column of water swirling up out of a pool;
    arms that end in curling waves; and pale cyan light for eyes."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    pool(r, 0.30)
    puddle(r, 0.46)
    swirl_column(r, 0.04, 0.50, 0.11, 0.19, ("pr_water_lt", "pr_foam", "pr_water_pale"), 0.05)
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.48), "body")
    r.joint("chest", (0, 0, 0.04), "pelvis", rest=(6, 0, 0))
    r.add("torso", E(0.21, 0.15, 0.25), "pr_water_dk", "chest", loc=(0, 0.01, 0.22))
    r.add("layer", E(0.19, 0.12, 0.21), "pr_water", "chest", loc=(-0.015, -0.035, 0.26))
    r.add("layer_lt", E(0.12, 0.07, 0.12), "pr_water_lt", "chest", loc=(-0.05, -0.085, 0.32))
    # Sheets of lighter water running down and round it.
    for k, (x0, x1, z0, z1) in enumerate(((-0.20, 0.10, 0.40, 0.05), (0.18, -0.06, 0.38, 0.10))):
        pts = [Vector((x0 + (x1 - x0) * f, -0.13 - 0.03 * math.sin(f * math.pi), z0 + (z1 - z0) * f))
               for f in (j / 5.0 for j in range(6))]
        chain(r, "chest", pts, 0.030, 0.018, "pr_water_lt" if k == 0 else "pr_water_pale")
    for sx, side in ((-1, "l"), (1, "r")):
        water_arm(r, side, sx, (sx * 0.22, 0.0, 0.38), 0.22, 0.18, 0.058, 12)
        r.add("shoulder", E(0.065, 0.065, 0.06), "pr_water", "shoulder_" + side, loc=(0, 0, 0.0))
    # The head up on a neck of water: set on the shoulders, the camera's
    # angle put the top of the chest over its chin and its eyes on the
    # shoulder line.
    r.joint("neck", (0, -0.02, 0.50), "chest")
    r.limb("neck", (0, 0, -0.06), (0, -0.01, 0.06), 0.07, "pr_water_dk", "neck", r_tip=0.065)
    r.joint("head", (0, 0, 0.02), "neck", rest=(-14, 0, 0))
    r.add("skull", E(0.125, 0.115, 0.13), "pr_water", "head", loc=(0, 0, 0.10))
    r.add("face", E(0.10, 0.06, 0.09), "pr_water_dk", "head", loc=(0, -0.07, 0.08))
    for sx in (-1, 1):
        r.add("eye", E(0.04, 0.02, 0.028), "pr_tide_eye_glow", "head", loc=(sx * 0.052, -0.118, 0.10))
    # The crest: a wave rising up the back of its head, over the top and
    # breaking forward over its brow, on a joint of its own so it can surge.
    # (A sheet of water swept across the head, not curls: from the front a
    # row of curls was a comb.)
    r.joint("crest", (0, 0, 0), "head")
    crest(r, "crest", 1.0, 0.10, 0.042)
    water_bits(r, TIDE, 0.035)
    return r.fit("tide_conjure", 48, tide_idle(0.0))


TIDE_REST = {"shoulder_l": (-10, 0, 0), "shoulder_r": (-10, 0, 0), "elbow_l": X(-20), "elbow_r": X(-20),
             "hand_l": X(-10), "hand_r": X(-10)}
# Both waves drawn up and back over its shoulders, the crest rearing...
TIDE_WIND = {"chest": X(-16), "head": X(-8), "_y": 0.05, "_z": 0.04, "crest": X(-20),
             "shoulder_l": (-160, 0, -20), "shoulder_r": (-160, 0, 20), "elbow_l": X(-30), "elbow_r": X(-30),
             "hand_l": X(-20), "hand_r": X(-20)}
# ...and crashing down on whatever is in front of it.
TIDE_CRASH = {"chest": X(26), "head": X(6), "_y": -0.16, "_z": -0.02, "crest": X(30),
              "shoulder_l": (-70, 0, 16), "shoulder_r": (-70, 0, -16), "elbow_l": X(-10), "elbow_r": X(-10),
              "hand_l": X(20), "hand_r": X(20)}


def water_clips(info, rest, wind, blow, marks=(0.40, 0.58, 1.0), spin=120.0, extra=None):
    """The five clips of a water Conjure: the column under it always turning
    (`spin` degrees a loop, a whole number of bands round so a loop comes
    back to its start), a wave thrown, and a death that falls in on itself
    in a splash and leaves a puddle."""
    def turning(v, t, speed=1.0):
        v["swirl"] = (0, 0, spin * t * speed)
        if extra:
            extra(v, t, speed)
        return v

    def idle(t):
        s = sn(t)
        v = dict(rest)
        v.update({"_z": 0.014 * s, "chest": X(2 * s), "head": (0, 0, 5 * sn(t, 0.3)), "crest": X(5 * sn(t, 0.25)),
                  "pelvis": (0, 0, 6 * s)})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x + 5 * s * k, y, z)
        return turning(v, t)

    def walk(t):
        # It flows: no stride, the body leaning into the way it is going and
        # riding on the column, which turns faster; the crest streams back.
        s = sn(t)
        v = dict(rest)
        v.update({"_z": 0.02 * abs(s), "chest": X(10 + 2 * s), "pelvis": (0, 0, 6 * s), "head": (0, 0, -4 * s),
                  "crest": X(-14 + 4 * sn(t * 2))})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x + 16 * s * k, y, z)
        return turning(v, t, 2.0)

    def attack(t):
        v = swing(t, rest, wind, blow, marks)
        return turning(v, t, 2.0)

    def hurt(t):
        k = math.sin(t * math.pi)
        v = dict(rest)
        v.update(struck(k, shoulder_l=(-30, 0, -20), shoulder_r=(-30, 0, 20), crest=X(-20)))
        return turning(v, t)

    def death(t):
        # It rears, and falls in on itself: the body drops into its own
        # column and the column into the ground, in a splash thrown up and
        # out that comes down as a puddle spreading where it stood.
        rise = ease(min(1.0, t / 0.2)) * (1.0 - ease(min(1.0, max(0.0, (t - 0.25) / 0.3))))
        fall = ease(min(1.0, max(0.0, (t - 0.2) / 0.45)))
        v = dict(rest)
        # (The Maelstrom's current hangs from the ground, not the body, so it
        # is taken down with it here: left up, it stood in the puddle alone.)
        v.update({"chest": X(-14 * rise + 24 * fall), "head": X(-10 * rise + 20 * fall), "_z": 0.05 * rise,
                  "crest": X(-20 * rise + 30 * fall), "~body": -0.99 * fall, "~current": -0.98 * fall,
                  "~swirl": -0.98 * ease(min(1.0, max(0.0, (t - 0.3) / 0.4)))})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x - 120 * rise, y, z - 30 * rise * k)
        turning(v, t, 2.0)
        if t >= 0.35:
            b = (t - 0.35) * 2.2
            for i, (p0, vel) in enumerate(info["drops"]):
                p = thrown(p0, vel, b, grav=4.2, floor=0.03)
                landed = p.z <= 0.031
                put(v, "edrop", i, p - Vector(p0), 0.0 if landed and t > 0.75 else 1.0)
        spread = ease(min(1.0, max(0.0, (t - 0.4) / 0.5)))
        v["~puddle"] = -0.99 + 0.99 * spread
        v["~pool"] = -0.99 * spread
        v["~whirl"] = -0.99 * spread
        return v

    return idle, walk, attack, hurt, death


tide_idle, tide_walk, tide_attack, tide_hurt, tide_death = water_clips(TIDE, TIDE_REST, TIDE_WIND, TIDE_CRASH)


# --- Maelstrom Conjure -----------------------------------------------------------------
def coral(r, parent, base, direction, length, width, colour, seed, depth=2):
    """A branch of coral: a stem that forks, and forks again, knobbed at the
    tips."""
    rnd = random.Random(seed)
    d = Vector(direction).normalized()
    tip = Vector(base) + d * length
    thin(r, "coral", tuple(base), tuple(tip), width, colour, parent, r_tip=width * 0.75)
    if depth <= 0:
        r.add("coral_tip", E(width * 1.1, width * 1.1, width * 1.1), colour, parent, loc=tuple(tip))
        return
    side = _perp(d)
    for k in (-1, 1):
        nd = (d + side * k * rnd.uniform(0.5, 0.9) + d.cross(side) * rnd.uniform(-0.3, 0.3)).normalized()
        coral(r, parent, tuple(tip), nd, length * 0.72, width * 0.72, colour, seed * 3 + k + 7, depth - 1)


def scallop(r, parent, loc, normal, size, colour="pr_shell", ridge="pr_shell_dk"):
    """A scallop shell lying on a surface, its ribs fanning from the hinge."""
    n = Vector(normal).normalized()
    ob = r.add("shell", E(size, size * 0.85, size * 0.25), colour, parent, loc=loc)
    ob.rotation_mode = "QUATERNION"
    q = Vector((0, 0, 1)).rotation_difference(n)
    ob.rotation_quaternion = q
    for k in range(5):
        a = math.radians(-60 + k * 30)
        rib_dir = q @ Vector((math.sin(a), math.cos(a) * 0.9, 0.0))
        start = Vector(loc) + q @ Vector((0, -size * 0.75, size * 0.18))
        thin(r, "rib", tuple(start + q @ Vector((0, 0, 0.004))), tuple(start + rib_dir * size * 1.45 + n * size * 0.12),
             size * 0.10, ridge, parent, r_tip=size * 0.12)


MAEL = {}
MAEL["drops"] = burst([(-0.18, -0.14), (0.18, -0.14), (0.0, -0.22), (-0.24, 0.04), (0.24, 0.04), (0.0, 0.14),
                       (-0.10, -0.20), (0.10, -0.20), (-0.16, 0.14), (0.16, 0.14), (-0.28, -0.08),
                       (0.28, -0.08)], 0.80, 0.78, 1.3)


def _mael_turn(v, t, speed):
    # The current round it turns the other way from the column: two bands of
    # it half a turn apart, so half a turn a loop brings it round.
    v["current"] = (0, 0, -180.0 * t * speed)
    v["whirl"] = (0, 0, 90.0 * t * speed)


def build_maelstrom_conjure():
    """The Tide Conjure grown huge and old: taller, wrapped in a spiral
    current that winds round it from a whirlpool at its base to its
    shoulders, plates of coral and shell grown over its shoulders and chest,
    and a crown of curling waves."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    # The whirlpool: rings of water turning round a dark eye, foam spiralling
    # in along them.
    r.joint("whirl", (0, 0, 0), "base")
    r.add("whirl_dk", E(0.56, 0.52, 0.02), "pr_water_dk", "whirl", loc=(0, 0, 0.010))
    for k, (rad, col) in enumerate(((0.50, "pr_water"), (0.36, "pr_water_lt"), (0.22, "pr_water"))):
        r.add("ring", TORUS(rad, 0.035), col, "whirl", loc=(0, 0, 0.022 + 0.004 * k))
    for k in range(4):
        a0 = k * math.tau / 4
        pts = [Vector(((0.54 - 0.34 * f) * math.cos(a0 + 2.2 * f), (0.50 - 0.32 * f) * math.sin(a0 + 2.2 * f), 0.05))
               for f in (j / 6.0 for j in range(7))]
        chain(r, "whirl", pts, 0.030, 0.014, "pr_foam")
    r.joint("pool", (0, 0, 0), "base")
    puddle(r, 0.60)
    swirl_column(r, 0.04, 0.56, 0.15, 0.24, ("pr_water_lt", "pr_foam", "pr_water_pale", "pr_water_lt"), 0.06)
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.54), "body")
    r.joint("chest", (0, 0, 0.05), "pelvis", rest=(6, 0, 0))
    r.add("torso", E(0.28, 0.20, 0.31), "pr_water_dk", "chest", loc=(0, 0.01, 0.28))
    r.add("layer", E(0.25, 0.16, 0.26), "pr_water", "chest", loc=(-0.02, -0.04, 0.32))
    r.add("layer_lt", E(0.15, 0.08, 0.15), "pr_water_lt", "chest", loc=(-0.07, -0.11, 0.40))
    # The chest: overlapping plates of shell, a great scallop over the heart,
    # coral grown up between them.
    scallop(r, "chest", (0.0, -0.20, 0.34), (0, -1, 0.35), 0.13)
    scallop(r, "chest", (-0.13, -0.16, 0.18), (-0.3, -1, 0.2), 0.08)
    scallop(r, "chest", (0.13, -0.16, 0.17), (0.3, -1, 0.2), 0.075)
    coral(r, "chest", (0.17, -0.12, 0.42), (0.6, -0.5, 1.0), 0.10, 0.022, "pr_coral_teal", 3, depth=1)
    for sx, side in ((-1, "l"), (1, "r")):
        water_arm(r, side, sx, (sx * 0.30, 0.0, 0.46), 0.27, 0.22, 0.072, 12)
        # Shoulders of shell, coral grown up out of them.
        r.add("shoulder", E(0.11, 0.11, 0.09), "pr_water", "shoulder_" + side, loc=(0, 0, 0.0))
        scallop(r, "shoulder_" + side, (sx * 0.03, 0.0, 0.08), (sx * 0.5, -0.1, 1.0), 0.12)
        coral(r, "shoulder_" + side, (sx * 0.06, 0.06, 0.10), (sx * 0.5, 0.3, 1.0), 0.13, 0.026,
              "pr_coral" if sx < 0 else "pr_coral_teal", 5 + (sx > 0))
        coral(r, "shoulder_" + side, (sx * -0.02, 0.08, 0.10), (sx * 0.1, 0.6, 1.0), 0.10, 0.022,
              "pr_coral_teal" if sx < 0 else "pr_coral", 8 + (sx > 0), depth=1)
    r.joint("neck", (0, -0.02, 0.56), "chest")
    r.joint("head", (0, 0, 0.02), "neck", rest=(-14, 0, 0))
    r.add("skull", E(0.15, 0.135, 0.15), "pr_water", "head", loc=(0, 0, 0.11))
    r.add("face", E(0.12, 0.07, 0.10), "pr_water_dk", "head", loc=(0, -0.08, 0.09))
    for sx in (-1, 1):
        r.add("eye", E(0.046, 0.02, 0.032), "pr_tide_eye_glow", "head", loc=(sx * 0.06, -0.138, 0.11))
    # The crown: the great crest over the top, and smaller waves curling up
    # round the brow in a ring.
    r.joint("crest", (0, 0, 0), "head")
    crest(r, "crest", 1.15, 0.12, 0.05)
    for k in range(4):
        a = math.radians(-135 + k * 90)
        out = Vector((math.sin(a), -math.cos(a), 0.0))
        base = Vector((0, 0, 0.16)) + out * 0.14
        sheet_curl(r, "head", tuple(base), tuple(out), (0, 0, 1), 0.08, 0.03, 0.045, 0.026, sweep=260, lip=0.6, n=6,
                   across=tuple(Vector((0, 0, 1)).cross(out)))
    # The current: a band of water winding up round it, a crest of foam
    # along its top, on a joint of its own that turns.
    r.joint("current", (0, 0, 0), "base")
    for k in range(2):
        pts = helix(0.10, 1.10, 0.46, 0.40, 1.0, 0.6 + k * math.pi, 16)
        chain(r, "current", pts, 0.048, 0.032, "pr_water_lt" if k == 0 else "pr_water_pale", "current")
        chain(r, "current", [p + Vector((0, 0, 0.042)) for p in pts[3:]], 0.019, 0.013, "pr_foam", "current_foam")
    water_bits(r, MAEL, 0.045)
    return r.fit("maelstrom_conjure", 64, mael_idle(0.0))


MAEL_REST = {"shoulder_l": (-10, 0, 0), "shoulder_r": (-10, 0, 0), "elbow_l": X(-20), "elbow_r": X(-20),
             "hand_l": X(-10), "hand_r": X(-10)}
MAEL_WIND = dict(TIDE_WIND)
MAEL_CRASH = dict(TIDE_CRASH)

mael_idle, mael_walk, mael_attack, mael_hurt, mael_death = water_clips(
    MAEL, MAEL_REST, MAEL_WIND, MAEL_CRASH, (0.42, 0.60, 1.0), spin=90.0, extra=_mael_turn)


# =================================================================================
#  Air: the Gale Conjure and the Cyclone Conjure
# =================================================================================
AIR = ("pr_air_white", "pr_air_blue", "pr_air_violet")


def hair(r, parent, n, root, spread, back, w):
    """Streamers swept back off a head in a fan: `n` of them, rooted across
    `root` either side, their ends spread `spread` either side and `back`
    behind, rising and then falling away, white, pale blue and violet."""
    cols = ("pr_air_white", "pr_air_blue", "pr_air_violet")
    for k in range(n):
        f0 = (k - (n - 1) / 2.0) / max(1.0, (n - 1) / 2.0)
        pts = [Vector((f0 * (root + (spread - root) * f), -0.04 + back * f,
                       0.10 * math.sin(f * math.pi * 0.9) - 0.08 * f * f - 0.02 * abs(f0)))
               for f in (j / 6.0 for j in range(7))]
        chain(r, parent, pts, w, w * 0.32, cols[k % 3] if n > 3 else cols[k], "streamer")


def leaf(r, parent, loc, size, colour, rot=(0, 0, 0)):
    """A leaf caught in the wind: a flat oval, tumbling."""
    r.add("leaf", E(size, size * 0.45, size * 0.12), colour, parent, loc=loc, rot=rot)


def cloud(r, parent, centre, rx, ry, rz, n, seed, cols=("pr_cloud", "pr_cloud_lt", "pr_cloud_dk"), puff=0.5):
    """A cloud: puffs heaped inside an ellipsoid, bigger towards its middle,
    so the outline is lumpy and each puff takes its own band of light."""
    rnd = random.Random(seed)
    cx, cy, cz = centre
    for k in range(n):
        while True:
            x, y, z = rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-1, 1)
            if x * x + y * y + z * z <= 1.0:
                break
        d = (x * x + y * y + z * z) ** 0.5
        s = puff * (1.15 - 0.5 * d) * rnd.uniform(0.8, 1.1)
        r.add("puff", E(rx * s, ry * s, rz * s), cols[k % len(cols)], parent,
              loc=(cx + x * rx * 0.75, cy + y * ry * 0.75, cz + z * rz * 0.75))


def build_gale_conjure():
    """A slender figure of wind: a faint pale core wound about by streamers of
    air -- white, pale blue, grey-violet -- a vortex for a lower body that
    does not touch the ground, long thin arms trailing ribbons, a pale mask of
    a face with glowing eyes, and leaves caught going round in it."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    # The vortex: three streamers wound down to a point a hand's breadth off
    # the ground, round a faint core, turning.
    r.joint("vortex", (0, 0, 0), "base")
    r.add("mist", C(0.15, 0.03, 0.46, squash_y=0.9), "pr_air_mist", "vortex", loc=(0, 0, 0.66))
    for k, col in enumerate(AIR):
        chain(r, "vortex", helix(0.14, 0.68, 0.025, 0.19, 1.5, k * math.tau / 3, 14), 0.022, 0.034, col, "streamer")
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.64), "body")
    r.joint("chest", (0, 0, 0.04), "pelvis", rest=(4, 0, 0))
    r.add("core", E(0.14, 0.10, 0.25), "pr_air_core", "chest", loc=(0, 0, 0.22))
    r.add("waist", E(0.10, 0.08, 0.10), "pr_air_core", "chest", loc=(0, 0, 0.0))
    # The wind wound round it, on a joint of its own that turns.
    r.joint("wind", (0, 0, 0), "pelvis")
    for k, col in enumerate(("pr_air_white", "pr_air_blue")):
        chain(r, "wind", helix(-0.02, 0.50, 0.17, 0.19, 1.1, 0.4 + k * math.pi, 14), 0.026, 0.022, col, "streamer")
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.17, 0, 0.40), "chest", rest=(0, -sx * 14, 0))
        r.limb("upper", (0, 0, 0), (0, 0, -0.27), 0.034, "pr_air_core", "shoulder_" + side, r_tip=0.03)
        r.joint("elbow_" + side, (0, 0, -0.27), "shoulder_" + side, rest=(-12, 0, 0))
        r.limb("fore", (0, 0, 0), (0, 0, -0.26), 0.03, "pr_air_core", "elbow_" + side, r_tip=0.022)
        r.joint("hand_" + side, (0, 0, -0.26), "elbow_" + side)
        for k in (-1, 0, 1):
            thin(r, "finger", (k * 0.012, -0.005, -0.01), (k * 0.03, -0.03, -0.10), 0.012, "pr_air_white",
                 "hand_" + side, r_tip=0.004)
        # The ribbons off the wrist: a joint of their own, so they stream.
        r.joint("trail_" + side, (0, 0.01, -0.20), "elbow_" + side)
        for k, col in enumerate(("pr_air_white", "pr_air_violet")):
            pts = [Vector((sx * (0.02 + 0.03 * f) + 0.025 * k * sx, 0.04 + 0.30 * f, -0.06 * f - 0.10 * f * f
                           + 0.04 * math.sin(f * 5 + k))) for f in (j / 6.0 for j in range(7))]
            chain(r, "trail_" + side, pts, 0.020, 0.008, col, "ribbon")
    r.joint("neck", (0, -0.01, 0.48), "chest")
    r.joint("head", (0, 0, 0.03), "neck", rest=(-14, 0, 0))
    r.add("skull", E(0.12, 0.12, 0.13), "pr_air_mist", "head", loc=(0, 0.01, 0.11))
    # The mask: a smooth pale oval with nothing behind its eyes but light --
    # dark round the light, or a pale face on a pale head has no eyes at all.
    r.add("mask", E(0.11, 0.055, 0.125), "pr_mask", "head", loc=(0, -0.08, 0.11))
    for sx in (-1, 1):
        r.add("hole", E(0.05, 0.02, 0.036), "pr_mask_dk", "head", loc=(sx * 0.05, -0.126, 0.13))
        r.add("eye", E(0.034, 0.02, 0.024), "pr_gale_eye_glow", "head", loc=(sx * 0.05, -0.137, 0.13))
    r.add("mouth", E(0.03, 0.012, 0.01), "pr_mask_dk", "head", loc=(0, -0.13, 0.05))
    # Streamers off the back of the head, swept back like hair in a gale and
    # fanned: kept in a bunch, from the front they stood up like a comb.
    r.joint("hair", (0, 0.05, 0.16), "head")
    hair(r, "hair", 3, 0.05, 0.16, 0.30, 0.028)
    # Leaves and dust caught going round in it.
    # (Two tiers of three, alike within a tier, so a third of a turn a loop
    # brings every leaf round onto the next one's place.)
    r.joint("leaves", (0, 0, 0), "base")
    for tier, (z, rad, col, a0) in enumerate(((0.36, 0.25, "pr_leaf", 0.0), (0.80, 0.30, "pr_leaf_brown", 1.05))):
        for k in range(3):
            a = a0 + k * math.tau / 3
            leaf(r, "leaves", (math.cos(a) * rad, math.sin(a) * rad, z), 0.045, col, rot=(0.6, 0.9, a))
            a2 = a + math.pi / 3
            r.add("dust", E(0.016, 0.016, 0.016), "pr_dust", "leaves",
                  loc=(math.cos(a2) * rad * 0.8, math.sin(a2) * rad * 0.8, z - 0.12))
    gale_bits(r, GALE)
    return r.fit("gale_conjure", 50, gale_idle(0.0))


# (The leaves it drops start where its two tiers of leaves go round.)
GALE = {"leaves": [((math.cos(a) * rad, math.sin(a) * rad, z),
                    (math.cos(a + 1.0) * 0.55, math.sin(a + 1.0) * 0.50, 0.3))
                   for (z, rad, a0) in ((0.36, 0.25, 0.0), (0.80, 0.30, 1.05))
                   for a in (a0 + k * math.tau / 3 for k in range(3))],
        "wisps": [((math.cos(a) * 0.12, math.sin(a) * 0.10, 0.90 + 0.10 * (k % 2)),
                   (math.cos(a) * 0.55, math.sin(a) * 0.45, 0.35 + 0.15 * (k % 3))) for k, a in
                  enumerate(k * math.tau / 6 for k in range(6))]}


def gale_bits(r, info):
    leaves = Bits(r, "eleaf")
    for i, (p0, _) in enumerate(info["leaves"]):
        leaves.add(p0, E(0.045, 0.02, 0.006), ("pr_leaf", "pr_leaf_dk", "pr_leaf_brown")[i % 3], rot=(0.3, 0.2 * i, i))
    wisps = Bits(r, "ewisp")
    for i, (p0, _) in enumerate(info["wisps"]):
        wisps.add(p0, C(0.04, 0.015, 0.16, squash_y=0.6), AIR[i % 3], rot=(math.pi / 2, 0, i))


GALE_REST = {"shoulder_l": (-8, 0, 0), "shoulder_r": (-8, 0, 0), "elbow_l": X(-16), "elbow_r": X(-16)}
# Both arms swept back and up across its body, the wind gathering...
GALE_WIND = {"chest": (-10, 0, 28), "head": (0, 0, -14), "_y": 0.04, "_z": 0.04,
             "shoulder_l": (-60, 50, -40), "shoulder_r": (-150, -30, 30), "elbow_l": X(-40), "elbow_r": X(-30)}
# ...and slashed across in front of it, the ribbons following.
GALE_SLASH = {"chest": (14, 0, -30), "head": (0, 0, 12), "_y": -0.14, "_z": 0.0,
              "shoulder_l": (-100, -20, 40), "shoulder_r": (-70, 40, -60), "elbow_l": X(-10), "elbow_r": X(-10)}


def air_clips(info, rest, wind_key, blow, marks=(0.38, 0.56, 1.0), turns=None, drift=0.05):
    """The five clips of an air Conjure: the wind on it always turning (each
    joint in `turns` a whole symmetry's worth a loop, so a loop comes back
    round), a blow struck with the gust behind it, and a death that blows
    itself apart -- its streamers flung out and thinning to nothing, what it
    carried falling out of it."""
    turns = turns or {}

    def turning(v, t, speed=1.0):
        for j, deg in turns.items():
            v[j] = (0, 0, deg * t * speed)
        return v

    def trails(v, t, amp=20.0, lean=0.0):
        v["trail_l"] = (lean + amp * sn(t, 0.1), 0, 8 * sn(t, 0.3))
        v["trail_r"] = (lean + amp * sn(t, 0.6), 0, -8 * sn(t, 0.8))
        v["hair"] = (lean * 0.6 + 10 * sn(t, 0.2), 0, 6 * sn(t))
        return v

    def idle(t):
        s = sn(t)
        v = dict(rest)
        v.update({"_z": drift * 0.4 * s, "chest": X(3 * s), "head": (0, 0, 6 * sn(t, 0.3)), "pelvis": (0, 0, 5 * s)})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x + 6 * s * k, y, z)
        return trails(turning(v, t), t)

    def walk(t):
        s = sn(t)
        v = dict(rest)
        v.update({"_z": drift * 0.5 * s, "chest": X(14 + 2 * s), "pelvis": (0, 0, 6 * s), "head": (0, 0, -5 * s)})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x + 14 + 10 * s * k, y, z)
        return trails(turning(v, t, 2.0), t, 26.0, -24.0)

    def attack(t):
        v = swing(t, rest, wind_key, blow, marks)
        i, k = phases(t, *marks)
        return trails(turning(v, t, 2.0), t, 30.0, [10 * k, 10 - 40 * k, -30 + 30 * k, 0][i])

    def hurt(t):
        k = math.sin(t * math.pi)
        v = dict(rest)
        v.update(struck(k, shoulder_l=(-30, 0, -20), shoulder_r=(-30, 0, 20)))
        return trails(turning(v, t), t, 30.0, -20 * k)

    def death(t):
        # It is blown apart: the wind on it flung out wider and wider and
        # thinning as it goes, the body fading into it, wisps thrown off it
        # and what it carried falling to the ground.
        out = ease(min(1.0, t / 0.7))
        gone = ease(min(1.0, max(0.0, (t - 0.15) / 0.6)))
        v = dict(rest)
        v.update({"chest": X(-10 * out), "head": X(-14 * out), "_z": 0.04 * out, "~body": -0.99 * gone})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x - 40 * out, y, z - 50 * out * k)
        turning(v, t, 3.0)
        # The swirl flung out and thinned: wider, and then nothing.
        for j in ("vortex", "funnel", "wind"):
            v["~" + j] = (1.0 + 0.9 * out) * (1.0 - gone) - 1.0
        # What it carried is handed over to the pieces that fall the moment
        # they are thrown -- the leaves at once, the rings when they fly
        # apart -- so nothing is drawn twice.
        v["~leaves"] = -0.99 if t >= 0.2 else 0.0
        for j in ("orbit1", "orbit2"):
            v["~" + j] = -0.99 if t >= 0.35 else 0.4 * out
        trails(v, t, 40.0, -50 * out)
        if t >= 0.2:
            b = (t - 0.2) / 0.8
            for i, (p0, vel) in enumerate(info.get("leaves", [])):
                p = thrown(p0, vel, b * 1.3, grav=1.6, floor=0.02)
                put(v, "eleaf", i, p - Vector(p0), 1.0, (300 * b + 40 * i, 200 * b, 90 * b))
            for i, (p0, vel) in enumerate(info.get("wisps", [])):
                put(v, "ewisp", i, Vector(vel) * b, max(0.0, 1.0 - b * 1.2), (0, 0, 120 * b))
        if t >= 0.35:
            b = (t - 0.35) / 0.65
            for i, (p0, vel) in enumerate(info.get("stones", [])):
                p = thrown(p0, vel, b * 1.2, grav=3.0, floor=0.04)
                put(v, "estone", i, p - Vector(p0), 1.0, (200 * b, 120 * b * (i % 2), 0))
        return v

    return idle, walk, attack, hurt, death


gale_idle, gale_walk, gale_attack, gale_hurt, gale_death = air_clips(
    GALE, GALE_REST, GALE_WIND, GALE_SLASH, turns={"vortex": 120.0, "wind": 180.0, "leaves": 120.0})


# --- Cyclone Conjure -------------------------------------------------------------------
def splinter(r, parent, loc, length, rot):
    r.add("splinter", C(0.020, 0.008, length, squash_y=0.6), "pr_splinter", parent, loc=loc, rot=rot)


CYCLONE = {"stones": [((math.cos(a) * 0.62, math.sin(a) * 0.62, 0.78 + 0.10 * math.sin(2 * a)),
                       (math.cos(a) * 0.30 - math.sin(a) * 0.25, math.sin(a) * 0.30 + math.cos(a) * 0.25, 0.2))
                      for a in (k * math.tau / 7 for k in range(7))]}


def cyclone_bits(r, info):
    stones = Bits(r, "estone")
    for i, (p0, _) in enumerate(info["stones"]):
        if i % 3 == 2:
            stones.add(p0, C(0.022, 0.009, 0.16, squash_y=0.6), "pr_splinter", rot=(0.4 * i, 1.2, 0))
        else:
            stones.add(p0, rock(0.055, 0.05, 0.045, 80 + i), ("pr_stone", "pr_stone_dk")[i % 2])


def build_cyclone_conjure():
    """The Gale Conjure grown huge: a funnel cloud with a torso -- a whirling
    cone of grey-white wind standing on its point, broad shoulders of cloud
    on top of it, a head of cloud with a crown of streamers swept back off
    it -- and two rings of stones and splinters it has torn up going round
    it, one each way."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    # The funnel: rings of wind stacked on its point, wider as they go up,
    # streamers wound round them, a cloud where it meets the body.
    # (A thin dark core only: a smooth pale cone the width of the rings read
    # from the front as an egg, not as wind.)
    r.joint("funnel", (0, 0, 0), "base")
    r.add("eye", C(0.17, 0.025, 0.64, squash_y=0.9), "pr_cloud_dk", "funnel", loc=(0, 0, 0.72))
    # (Its widest ring no wider than the body above it and a little under it:
    # level with the waist and wider, from above the body sat in a bowl.)
    for k in range(9):
        f = k / 8.0
        rad = 0.05 + 0.22 * f
        r.add("ring", TORUS(rad, 0.026 + 0.016 * f), ("pr_cloud_lt", "pr_cloud", "pr_air_white", "pr_cloud_dk")[k % 4],
              "funnel", loc=(0.02 * math.sin(k * 1.7), 0.02 * math.cos(k * 1.3), 0.08 + 0.074 * k),
              rot=(0.14 * math.sin(k * 1.1), 0.12 * math.cos(k * 0.7), 0))
    for k, col in enumerate(("pr_air_white", "pr_air_blue", "pr_air_violet")):
        chain(r, "funnel", helix(0.05, 0.68, 0.05, 0.29, 1.6, k * math.tau / 3, 16), 0.022, 0.036, col, "streamer")
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.74), "body")
    cloud(r, "pelvis", (0, 0, 0.02), 0.30, 0.24, 0.12, 9, 11)
    r.joint("chest", (0, 0, 0.06), "pelvis", rest=(6, 0, 0))
    cloud(r, "chest", (0, 0.02, 0.24), 0.30, 0.22, 0.22, 12, 12)
    # Broad shoulders of cloud.
    cloud(r, "chest", (-0.30, 0.02, 0.40), 0.20, 0.18, 0.14, 7, 13)
    cloud(r, "chest", (0.30, 0.02, 0.40), 0.20, 0.18, 0.14, 7, 14)
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.40, 0.02, 0.38), "chest", rest=(0, -sx * 12, 0))
        cloud(r, "shoulder_" + side, (0, 0, -0.14), 0.09, 0.09, 0.16, 5, 15 + (sx > 0), puff=0.55)
        r.joint("elbow_" + side, (0, 0, -0.30), "shoulder_" + side, rest=(-14, 0, 0))
        cloud(r, "elbow_" + side, (0, 0, -0.13), 0.085, 0.085, 0.15, 5, 17 + (sx > 0), puff=0.55)
        r.joint("hand_" + side, (0, 0, -0.28), "elbow_" + side)
        cloud(r, "hand_" + side, (0, -0.02, -0.06), 0.11, 0.10, 0.10, 5, 19 + (sx > 0), puff=0.6)
        # Wind wound round the forearm.
        chain(r, "elbow_" + side, [Vector((0.085 * math.cos(a), 0.085 * math.sin(a), -0.02 - 0.05 * a))
                                   for a in (j * 0.9 for j in range(6))], 0.016, 0.014, "pr_air_white", "streamer")
    r.joint("neck", (0, -0.02, 0.50), "chest")
    r.joint("head", (0, 0, 0.03), "neck", rest=(-14, 0, 0))
    cloud(r, "head", (0, 0.01, 0.10), 0.14, 0.13, 0.12, 7, 21, puff=0.62)
    r.add("mask", E(0.10, 0.05, 0.10), "pr_cloud_dk", "head", loc=(0, -0.10, 0.10))
    for sx in (-1, 1):
        r.add("eye", E(0.032, 0.02, 0.024), "pr_gale_eye_glow", "head", loc=(sx * 0.045, -0.142, 0.115))
    # The crown: streamers swept back off the brow.
    r.joint("hair", (0, 0.02, 0.18), "head")
    hair(r, "hair", 5, 0.06, 0.30, 0.42, 0.032)
    # What it has torn up: two rings going round it, one each way, each
    # tilted on one joint and turned on another inside it -- turned outside
    # the tilt, every stone went round at its own height, and the ring came
    # apart. A ring of stones above, a ring of splinters lower down, each one
    # like the next, so a loop that turns a ring by one place comes round.
    for ring, (rad, z, n, tilt) in enumerate(((0.62, 0.86, 7, (10, 4, 0)), (0.46, 0.40, 6, (-8, 6, 0)))):
        j = "orbit%d" % (ring + 1)
        r.joint(j, (0, 0, z), "base", rest=tilt)
        r.joint("spin%d" % (ring + 1), (0, 0, 0), j)
        for k in range(n):
            a = k * math.tau / n
            p = (math.cos(a) * rad, math.sin(a) * rad, 0.0)
            if ring:
                splinter(r, "spin2", p, 0.18, (0.5, 1.1, a))
            else:
                r.add("stone", rock(0.058, 0.052, 0.046, 84), "pr_stone", "spin1", loc=p, rot=(0, 0, a))
    cyclone_bits(r, CYCLONE)
    return r.fit("cyclone_conjure", 66, cyclone_idle(0.0))


CYCLONE_REST = {"shoulder_l": (-10, 0, 0), "shoulder_r": (-10, 0, 0), "elbow_l": X(-20), "elbow_r": X(-20)}
# Both arms flung up, the whole of it winding...
CYCLONE_WIND = {"chest": (-12, 0, 30), "head": X(-8), "_y": 0.04, "_z": 0.03,
                "shoulder_l": (-150, 0, -30), "shoulder_r": (-150, 0, 30), "elbow_l": X(-30), "elbow_r": X(-30)}
# ...and the whole of it turning through, arms out, a blow like a gust.
CYCLONE_SPIN = {"chest": (16, 0, -40), "head": X(6), "_y": -0.12,
                "shoulder_l": (-60, 0, 60), "shoulder_r": (-60, 0, -60), "elbow_l": X(-8), "elbow_r": X(-8)}

cyclone_idle, cyclone_walk, cyclone_attack, cyclone_hurt, cyclone_death = air_clips(
    CYCLONE, CYCLONE_REST, CYCLONE_WIND, CYCLONE_SPIN, (0.40, 0.58, 1.0),
    turns={"funnel": 120.0, "spin1": 360.0 / 7, "spin2": -60.0}, drift=0.03)


# =================================================================================
#  Lightning: the Storm Conjure and the Thunder Conjure
# =================================================================================
STORM = ("pr_storm", "pr_storm_lt", "pr_storm_dk", "pr_storm_violet")


def bolt(r, parent, a, b, n=5, jag=0.30, width=0.024, colour="pr_bolt_glow", seed=0, fork=None):
    """A bolt of lightning from a to b: n straight runs, each kink pushed
    aside by up to `jag` of a run's length; `fork` = (at, to) sends a branch
    off the kink nearest `at` (0..1) to the point `to`."""
    rnd = random.Random(seed * 101 + 7)
    a, b = Vector(a), Vector(b)
    d = b - a
    s1 = _perp(d.normalized())
    s2 = d.normalized().cross(s1)
    pts = [a]
    for k in range(1, n):
        off = (s1 * rnd.uniform(-1, 1) + s2 * rnd.uniform(-0.6, 0.6)) * jag * d.length / n
        pts.append(a + d * (k / float(n)) + off)
    pts.append(b)
    tube(r, "bolt", pts, [width * (1.0 - 0.25 * k / n) for k in range(n + 1)], colour, parent, segments=5)
    if fork:
        at, to = fork
        p = pts[max(1, min(n - 1, int(round(at * n))))]
        bolt(r, parent, p, to, max(2, n // 2), jag, width * 0.75, colour, seed + 13)
    return pts


def lit(t, n, m, salt=0):
    """Which `m` of `n` flickering things are lit at time t: a fresh choice
    every frame of every clip, the same choice every time that frame is
    drawn. (t is snapped to sixtieths: every clip's frames fall on them.)"""
    u = int(round(t * 60))
    score = [((math.sin(u * 12.9898 + i * 78.233 + salt * 37.719) * 43758.5453) % 1.0, i) for i in range(n)]
    return set(i for _, i in sorted(score)[:m])


def crackle(v, t, sets, m, salt=0):
    """Light `m` of the flickering bolt sets in `sets` this frame, the rest
    hidden (they are built hidden: shown only when a pose says so)."""
    on = lit(t, len(sets), m, salt)
    for i, j in enumerate(sets):
        v["~" + j] = 0.0 if i in on else -1.0
    return v


def zap_set(r, name, parent, bolts, width, seed):
    """A set of bolts on a joint of its own, built hidden: what crackle()
    lights a frame at a time."""
    r.joint(name, (0, 0, 0), parent)
    for k, (a, b, fork) in enumerate(bolts):
        bolt(r, name, a, b, 4, 0.40, width, "pr_bolt_glow" if k % 2 else "pr_bolt_hot_glow", seed + k, fork)
    r.j[name].scale = (0.01, 0.01, 0.01)
    return name


def mist(r, parent, z, w, seed):
    """A lower body of mist: cloud thinning downwards to wisps that do not
    reach the ground."""
    cloud(r, parent, (0, 0, z), w, w * 0.85, w * 0.75, 9, seed, STORM)
    cloud(r, parent, (0, 0.01, z - w * 0.95), w * 0.70, w * 0.62, w * 0.55, 7, seed + 1, STORM)
    cloud(r, parent, (0, 0.02, z - w * 1.70), w * 0.42, w * 0.38, w * 0.36, 5, seed + 2, STORM)
    for k in range(3):
        a = k * math.tau / 3 + 0.5
        r.add("wisp", E(w * 0.14, w * 0.14, w * 0.22), STORM[k % 4], parent,
              loc=(math.cos(a) * w * 0.25, math.sin(a) * w * 0.25, z - w * 2.25 - 0.03 * k))


def eyes_crackling(r, parent, x, y, z, size):
    """Eyes of lightning: white-hot, sparks jumping off them."""
    for sx in (-1, 1):
        r.add("eye", E(size, size * 0.5, size * 0.7), "pr_storm_eye_glow", parent, loc=(sx * x, y, z))
        for k, (dx, dz) in enumerate(((1.6, 0.9), (1.8, -0.5), (-0.6, 1.5))):
            thin(r, "spark", (sx * x + sx * size * 0.6 * dx * 0.5, y - 0.004, z + size * 0.5 * dz * 0.5),
                 (sx * x + sx * size * dx, y - 0.004, z + size * dz), size * 0.22, "pr_bolt_glow", parent,
                 r_tip=0.004)


def build_storm_conjure():
    """A body of dark thundercloud, slate and violet-grey, with jagged
    lightning glowing inside it -- a different stroke each moment -- eyes of
    lightning crackling, arms of cloud with fingers of lightning, and a
    lower body of mist that does not touch the ground."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.62), "body")
    mist(r, "pelvis", -0.02, 0.17, 30)
    r.joint("chest", (0, 0, 0.06), "pelvis", rest=(6, 0, 0))
    cloud(r, "chest", (0, 0.02, 0.26), 0.25, 0.19, 0.25, 14, 31, STORM)
    # Lightning in it: a stroke always there, and three more that come and go.
    bolt(r, "chest", (-0.05, -0.19, 0.42), (0.04, -0.19, 0.08), 4, 0.5, 0.022, "pr_bolt_glow", 1,
         fork=(0.5, (0.15, -0.16, 0.16)))
    STORM_C["zaps"] = [
        zap_set(r, "zap0", "chest", [((-0.17, -0.12, 0.40), (-0.02, -0.19, 0.20), None),
                                     ((0.10, -0.17, 0.44), (0.20, -0.10, 0.18), None)], 0.022, 10),
        zap_set(r, "zap1", "chest", [((0.16, -0.14, 0.36), (-0.06, -0.19, 0.30), (0.5, (-0.12, -0.15, 0.12))),
                                     ((-0.20, -0.08, 0.20), (-0.10, -0.15, 0.06), None)], 0.022, 20),
        zap_set(r, "zap2", "chest", [((-0.08, -0.18, 0.46), (0.12, -0.17, 0.30), None),
                                     ((0.0, 0.18, 0.40), (0.10, 0.17, 0.10), None),
                                     ((-0.12, 0.17, 0.30), (-0.02, 0.18, 0.06), None)], 0.022, 30)]
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.24, 0.02, 0.40), "chest", rest=(0, -sx * 5, 0))
        cloud(r, "shoulder_" + side, (0, 0, -0.02), 0.11, 0.10, 0.09, 5, 32 + (sx > 0), STORM, puff=0.6)
        cloud(r, "shoulder_" + side, (0, 0, -0.14), 0.07, 0.07, 0.13, 4, 34 + (sx > 0), STORM, puff=0.65)
        r.joint("elbow_" + side, (0, 0, -0.26), "shoulder_" + side, rest=(-14, 0, 0))
        cloud(r, "elbow_" + side, (0, 0, -0.11), 0.065, 0.065, 0.12, 4, 36 + (sx > 0), STORM, puff=0.65)
        r.joint("hand_" + side, (0, 0, -0.23), "elbow_" + side)
        cloud(r, "hand_" + side, (0, -0.01, -0.02), 0.07, 0.065, 0.06, 4, 38 + (sx > 0), STORM, puff=0.7)
        # Fingers of lightning.
        for k in range(4):
            a = math.radians(-45 + k * 30)
            bolt(r, "hand_" + side, (math.sin(a) * 0.04, -0.03, -0.05),
                 (math.sin(a) * 0.10, -0.09 - 0.02 * (k % 2), -0.17 - 0.03 * (k % 2)), 3, 0.5, 0.016,
                 "pr_bolt_glow", 40 + k + 4 * (sx > 0))
    r.joint("neck", (0, -0.02, 0.48), "chest")
    r.joint("head", (0, 0, 0.03), "neck", rest=(-14, 0, 0))
    cloud(r, "head", (0, 0.01, 0.10), 0.13, 0.12, 0.12, 8, 42, STORM, puff=0.62)
    r.add("face", E(0.10, 0.05, 0.08), "pr_storm_dk", "head", loc=(0, -0.10, 0.10))
    eyes_crackling(r, "head", 0.05, -0.142, 0.11, 0.03)
    storm_bits(r, STORM_C)
    strike_bolt(r, 0.55, 55)
    return r.fit("storm_conjure", 50, storm_idle(0.0))


STORM_C = {"zaps": [],
           "puffs": [((math.cos(a) * 0.10, math.sin(a) * 0.08, 0.80 + 0.12 * (k % 3)),
                      (math.cos(a) * 0.55, math.sin(a) * 0.45, 0.30 + 0.12 * (k % 2))) for k, a in
                     enumerate(k * math.tau / 7 + 0.2 for k in range(7))]}


def discharge(r, name, centre, n, reach, seed, width=0.03):
    """The discharge a lightning Conjure dies in: bolts from its heart down
    to the ground all round it, built hidden; and the scorch they leave."""
    r.joint(name, centre, "base")
    for k in range(n):
        a = k * math.tau / n + 0.3
        to = Vector((math.cos(a) * reach, math.sin(a) * reach * 0.9, 0.02 - centre[2]))
        bolt(r, name, (0, 0, 0), to, 5, 0.35, width, "pr_bolt_hot_glow" if k % 2 else "pr_bolt_glow", seed + k,
             fork=(0.6, to * 0.7 + Vector((math.cos(a + 0.7) * reach * 0.35, math.sin(a + 0.7) * reach * 0.3, 0))))
    r.j[name].scale = (0.01, 0.01, 0.01)
    r.joint("scorch", (0, 0, 0), "base")
    r.add("scorch", E(reach * 0.55, reach * 0.48, 0.012), "pr_scorch", "scorch", loc=(0, 0, 0.006))
    r.add("scorch_in", E(reach * 0.30, reach * 0.26, 0.014), "pr_scorch_dk", "scorch", loc=(0, 0, 0.008))
    r.j["scorch"].scale = (0.01, 0.01, 0.01)


def storm_bits(r, info, size=0.07):
    discharge(r, "flash", (0, 0, 0.92), 7, 0.62, 50)
    puffs = Bits(r, "epuff")
    for i, (p0, _) in enumerate(info["puffs"]):
        puffs.add(p0, E(size, size * 0.9, size * 0.8), STORM[i % 4])


STORM_REST = {"shoulder_l": (-12, 0, 0), "shoulder_r": (-12, 0, 0), "elbow_l": X(-22), "elbow_r": X(-22),
              "hand_l": X(-10), "hand_r": X(-10)}
# Arms up, the charge gathering in it...
STORM_CHARGE = {"chest": X(-14), "head": X(-10), "_y": 0.04, "_z": 0.05,
                "shoulder_l": (-150, 0, -26), "shoulder_r": (-150, 0, 26), "elbow_l": X(-34), "elbow_r": X(-34),
                "hand_l": X(-20), "hand_r": X(-20)}
# ...and thrown forward out of both hands, the fingers of lightning spread.
STORM_THROW = {"chest": X(22), "head": X(4), "_y": -0.12,
               "shoulder_l": (-84, 0, 14), "shoulder_r": (-84, 0, -14), "elbow_l": X(-6), "elbow_r": X(-6),
               "hand_l": X(40), "hand_r": X(40)}


def storm_clips(info, rest, wind, blow, marks=(0.42, 0.58, 1.0), arcs=(), crown=(), bob=0.05):
    """The five clips of a lightning Conjure: always drifting, the lightning
    in it a different stroke every frame and more of it when it strikes; a
    death that discharges into the ground all round it and leaves a cloud
    that comes apart and a scorch."""
    def flicker_all(v, t, m=1, extra=0):
        crackle(v, t, info["zaps"], min(len(info["zaps"]), m + extra))
        if arcs:
            crackle(v, t, arcs, min(len(arcs), 2 + extra), 1)
        if crown:
            crackle(v, t, crown, 1, 2)
        return v

    def idle(t):
        s = sn(t)
        v = dict(rest)
        v.update({"_z": bob * 0.4 * s, "chest": X(3 * s), "head": (0, 0, 6 * sn(t, 0.3)), "pelvis": (0, 0, 4 * s)})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x + 5 * s * k, y, z)
        if "mantle" in info:
            v["cape"] = X(4 * s)
        return flicker_all(v, t)

    def walk(t):
        s = sn(t)
        v = dict(rest)
        v.update({"_z": bob * 0.5 * s, "chest": X(12 + 2 * s), "pelvis": (0, 0, 6 * s), "head": (0, 0, -4 * s)})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x + 10 + 12 * s * k, y, z)
        if "mantle" in info:
            v["cape"] = (-16 - 4 * sn(t * 2), 0, 3 * s)
        return flicker_all(v, t)

    def attack(t):
        v = swing(t, rest, wind, blow, marks)
        i, k = phases(t, *marks)
        if "mantle" in info:
            v["cape"] = X([-16 * k, -16 + 30 * k, 14 - 14 * k, 0][i])
        # Everything lit at the strike, and the bolt it throws.
        v = flicker_all(v, t, 1, 1 if i == 0 else 3 if i == 1 else 0)
        v["~strike"] = 0.0 if (i == 1 and k > 0.5) or (i == 2 and k < 0.35) else -1.0
        return v

    def hurt(t):
        k = math.sin(t * math.pi)
        v = dict(rest)
        v.update(struck(k, shoulder_l=(-30, 0, -20), shoulder_r=(-30, 0, 20)))
        if "mantle" in info:
            v["cape"] = X(-14 * k)
        return flicker_all(v, t, 2)

    def death(t):
        # It discharges: everything in it goes into the ground at once in
        # bolts all round it, and what is left is cloud -- billowing out,
        # thinning, and gone -- and a scorch on the ground.
        flash = 1.0 if 0.1 < t < 0.5 else 0.0
        swell = ease(min(1.0, max(0.0, (t - 0.25) / 0.4)))
        gone = ease(min(1.0, max(0.0, (t - 0.45) / 0.5)))
        v = dict(rest)
        # (Arms flung out sideways, not up: up, from the side the near one
        # reached the top of the frame.)
        v.update({"chest": X(-16 * (1 - gone)), "head": X(-16), "_z": 0.04,
                  "~body": (1.0 + 0.2 * swell) * (1.0 - gone) - 1.0})
        for side, k in (("l", 1), ("r", -1)):
            x, y, z = rest.get("shoulder_" + side, (0, 0, 0))
            v["shoulder_" + side] = (x - 25 * swell, y, z - 55 * swell * k)
        flicker_all(v, t, 3 if flash else 0)
        if t > 0.45:
            for j in info["zaps"] + list(arcs) + list(crown):
                v["~" + j] = -1.0
        v["~flash"] = 0.0 if flash else -1.0
        v["~scorch"] = 0.0 if t > 0.15 else -1.0
        if t >= 0.3:
            b = (t - 0.3) / 0.7
            for i, (p0, vel) in enumerate(info["puffs"]):
                put(v, "epuff", i, Vector(vel) * b, max(0.0, (1.0 + 0.6 * b) * (1.0 - b * 1.05)))
        return v

    return idle, walk, attack, hurt, death


def strike_bolt(r, length, seed, width=0.026):
    """The bolt a lightning Conjure throws, forward from between its hands:
    built hidden, shown the frame the blow lands."""
    r.joint("strike", (0, -0.20, 0.60), "base")
    bolt(r, "strike", (0, 0, 0), (0.02, -length, -0.30), 6, 0.30, width, "pr_bolt_hot_glow", seed,
         fork=(0.55, (0.16, -length * 0.8, -0.38)))
    bolt(r, "strike", (0.05, 0, 0.02), (-0.08, -length * 0.9, -0.36), 5, 0.35, width * 0.8, "pr_bolt_glow", seed + 5)
    r.j["strike"].scale = (0.01, 0.01, 0.01)


storm_idle, storm_walk, storm_attack, storm_hurt, storm_death = storm_clips(STORM_C, STORM_REST, STORM_CHARGE,
                                                                            STORM_THROW)


# --- Thunder Conjure -------------------------------------------------------------------
THUNDER = {"zaps": [], "mantle": True,
           "puffs": [((math.cos(a) * 0.14, math.sin(a) * 0.10, 1.00 + 0.14 * (k % 3)),
                      (math.cos(a) * 0.70, math.sin(a) * 0.58, 0.40 + 0.14 * (k % 2))) for k, a in
                     enumerate(k * math.tau / 9 + 0.2 for k in range(9))]}
THUNDER_ARCS = []
THUNDER_CROWN = []


def build_thunder_conjure():
    """The Storm Conjure grown huge: a mantle of storm-cloud over its
    shoulders and down its back, a chest where the lightning gathers into one
    glowing heart and goes out from it, a crown of forked lightning, and arcs
    crawling over its arms, never the same two moments together."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.70), "body")
    mist(r, "pelvis", -0.02, 0.22, 60)
    r.joint("chest", (0, 0, 0.07), "pelvis", rest=(6, 0, 0))
    cloud(r, "chest", (0, 0.03, 0.30), 0.31, 0.23, 0.30, 16, 61, STORM)
    # The heart: the lightning gathered into a ball of light in its chest,
    # a ring of cloud round it, strokes of it running out to the edges.
    r.add("heart_halo", E(0.15, 0.06, 0.15), "pr_bolt_glow", "chest", loc=(0, -0.20, 0.32))
    r.add("heart", E(0.09, 0.05, 0.09), "pr_bolt_hot_glow", "chest", loc=(0, -0.235, 0.32))
    r.add("heart_rim", TORUS(0.15, 0.03), "pr_storm_dk", "chest", loc=(0, -0.21, 0.32), rot=(math.pi / 2, 0, 0))
    for k in range(5):
        a = math.radians(-160 + k * 70)
        to = Vector((math.cos(a) * 0.28, -0.19 + 0.04 * abs(math.cos(a)), 0.32 + math.sin(a) * 0.26))
        bolt(r, "chest", (math.cos(a) * 0.14, -0.22, 0.32 + math.sin(a) * 0.14), to, 3, 0.4, 0.022,
             "pr_bolt_glow", 62 + k)
    THUNDER["zaps"] = [
        zap_set(r, "zap0", "chest", [((-0.24, -0.12, 0.50), (-0.12, -0.20, 0.14), None)], 0.024, 70),
        zap_set(r, "zap1", "chest", [((0.24, -0.12, 0.48), (0.14, -0.20, 0.10), None)], 0.024, 72),
        zap_set(r, "zap2", "chest", [((0.0, 0.22, 0.52), (0.12, 0.21, 0.12), (0.5, (-0.14, 0.20, 0.10)))], 0.024, 74)]
    # The mantle: storm-cloud heaped over the shoulders and hung down the
    # back on a joint of its own.
    r.joint("cape", (0, 0.16, 0.50), "chest", rest=(10, 0, 0))
    cloud(r, "cape", (0, 0.04, -0.10), 0.34, 0.14, 0.24, 12, 64, ("pr_storm_dk", "pr_storm", "pr_storm_violet"))
    cloud(r, "cape", (0, 0.08, -0.40), 0.30, 0.12, 0.20, 10, 65, ("pr_storm_dk", "pr_storm_violet", "pr_storm"))
    arcs = []
    for sx, side in ((-1, "l"), (1, "r")):
        r.joint("shoulder_" + side, (sx * 0.33, 0.02, 0.50), "chest", rest=(0, -sx * 6, 0))
        cloud(r, "shoulder_" + side, (sx * 0.02, 0.02, 0.02), 0.17, 0.15, 0.13, 7, 66 + (sx > 0),
              ("pr_storm_dk", "pr_storm", "pr_storm_violet"), puff=0.6)
        cloud(r, "shoulder_" + side, (0, 0, -0.17), 0.09, 0.09, 0.16, 4, 68 + (sx > 0), STORM, puff=0.65)
        r.joint("elbow_" + side, (0, 0, -0.32), "shoulder_" + side, rest=(-14, 0, 0))
        cloud(r, "elbow_" + side, (0, 0, -0.14), 0.085, 0.085, 0.15, 4, 70 + (sx > 0), STORM, puff=0.65)
        r.joint("hand_" + side, (0, 0, -0.29), "elbow_" + side)
        cloud(r, "hand_" + side, (0, -0.01, -0.03), 0.10, 0.09, 0.09, 5, 72 + (sx > 0), STORM, puff=0.7)
        for k in range(4):
            a = math.radians(-45 + k * 30)
            bolt(r, "hand_" + side, (math.sin(a) * 0.05, -0.04, -0.07),
                 (math.sin(a) * 0.13, -0.12 - 0.02 * (k % 2), -0.22 - 0.03 * (k % 2)), 3, 0.5, 0.019,
                 "pr_bolt_glow", 74 + k + 4 * (sx > 0))
        # Arcs crawling over the arm: three ways of it, one or two at a time.
        for k in range(3):
            j = "arc%d%s" % (k, side)
            r.joint(j, (0, 0, 0), "shoulder_" + side if k < 2 else "elbow_" + side)
            seed = 80 + k * 3 + 10 * (sx > 0)
            if k < 2:
                bolt(r, j, (sx * 0.08 * (1 - 2 * k), -0.09, 0.0), (sx * -0.06 * (1 - 2 * k), -0.08, -0.30), 4, 0.5,
                     0.018, "pr_bolt_glow", seed)
            else:
                bolt(r, j, (sx * 0.07, -0.08, -0.02), (sx * -0.05, -0.08, -0.26), 4, 0.5, 0.018, "pr_bolt_hot_glow",
                     seed)
            r.j[j].scale = (0.01, 0.01, 0.01)
            arcs.append(j)
    THUNDER_ARCS[:] = arcs
    r.joint("neck", (0, -0.02, 0.58), "chest")
    r.joint("head", (0, 0, 0.03), "neck", rest=(-14, 0, 0))
    cloud(r, "head", (0, 0.01, 0.11), 0.15, 0.14, 0.14, 9, 76, STORM, puff=0.62)
    r.add("face", E(0.12, 0.06, 0.09), "pr_storm_dk", "head", loc=(0, -0.11, 0.11))
    eyes_crackling(r, "head", 0.058, -0.162, 0.12, 0.036)
    # The crown: forked lightning standing up off the head, two ways of it
    # flickering between, so the crown is never still.
    crown = []
    for c in range(2):
        j = "crown%d" % c
        r.joint(j, (0, 0, 0), "head")
        for k in range(5):
            a = math.radians(-150 + k * 60)
            root = Vector((math.cos(a) * 0.10, math.sin(a) * 0.08 + 0.02, 0.22))
            tip = root + Vector((math.cos(a) * 0.08, math.sin(a) * 0.06, 0.24 + 0.05 * (k % 2) - 0.04 * abs(k - 2)))
            bolt(r, j, root, tip, 3, 0.5, 0.024, "pr_bolt_hot_glow" if (k + c) % 2 else "pr_bolt_glow", 90 + k + 7 * c,
                 fork=(0.6, tip + Vector((math.cos(a + 0.9) * 0.07, math.sin(a + 0.9) * 0.05, -0.02))))
        if c:
            r.j[j].scale = (0.01, 0.01, 0.01)
        crown.append(j)
    THUNDER_CROWN[:] = crown
    discharge(r, "flash", (0, 0, 1.08), 9, 0.78, 100, width=0.034)
    puffs = Bits(r, "epuff")
    for i, (p0, _) in enumerate(THUNDER["puffs"]):
        puffs.add(p0, E(0.09, 0.08, 0.07), STORM[i % 4])
    strike_bolt(r, 0.70, 110, width=0.03)
    return r.fit("thunder_conjure", 66, thunder_idle(0.0))


THUNDER_REST = {"shoulder_l": (-12, 0, 0), "shoulder_r": (-12, 0, 0), "elbow_l": X(-22), "elbow_r": X(-22),
                "hand_l": X(-10), "hand_r": X(-10)}
THUNDER_CHARGE = dict(STORM_CHARGE)
# Down with both fists on whatever is in front of it, the thunder with them.
THUNDER_SLAM = {"chest": X(24), "head": X(2), "_y": -0.12, "_z": -0.02,
                "shoulder_l": (-80, 0, 12), "shoulder_r": (-80, 0, -12), "elbow_l": X(-8), "elbow_r": X(-8),
                "hand_l": X(20), "hand_r": X(20)}

thunder_idle, thunder_walk, thunder_attack, thunder_hurt, thunder_death = storm_clips(
    THUNDER, THUNDER_REST, THUNDER_CHARGE, THUNDER_SLAM, (0.44, 0.60, 1.0), arcs=THUNDER_ARCS, crown=THUNDER_CROWN,
    bob=0.04)


# =================================================================================
#  The Quintessence
# =================================================================================
# The five elements' lights, in the order the orbs go round: fire, earth,
# water, air, lightning.
FIVE = ("pr_flame_o_glow", "pr_amber_glow", "pr_tide_eye_glow", "pr_gale_eye_glow", "pr_bolt_glow")
QUINT = {"tongues": [], "crown": []}


def _orb(r, parent, loc, kind, s):
    """One of the five orbs that go round the Quintessence, each its element
    in small: a coal of fire with flames off it, a stone with a crystal in
    it, a drop of water, a little whirlwind, a ball of lightning with sparks
    off it."""
    x, y, z = loc
    if kind == 0:
        r.add("orb", E(s, s, s), "pr_flame_o_glow", parent, loc=loc)
        for k in range(3):
            a = k * math.tau / 3
            tip = (x + math.cos(a) * s * 0.7, y + math.sin(a) * s * 0.7, z + s * 1.9)
            thin(r, "orb_flame", (x + math.cos(a) * s * 0.5, y + math.sin(a) * s * 0.5, z + s * 0.3), tip, s * 0.55,
                 "pr_flame_r_glow" if k % 2 else "pr_flame_y_glow", parent, r_tip=s * 0.1)
    elif kind == 1:
        r.add("orb", rock(s * 1.1, s * 1.0, s * 0.95, 95), "pr_stone", parent, loc=loc)
        aim(r, r.add("orb_xtal", prism(s * 0.45, s * 1.6), "pr_xtal_a", parent, loc=(x, y, z + s * 0.4)), (0.3, 0.2, 1))
    elif kind == 2:
        # A drop: round below, drawn up to a point; a ring of foam round it
        # would be an eye.
        r.add("orb", E(s, s, s), "pr_water", parent, loc=loc)
        r.limb("orb_tip", (x, y, z + s * 0.4), (x, y, z + s * 1.9), s * 0.62, "pr_water", parent, r_tip=s * 0.08)
        r.add("orb_lt", E(s * 0.38, s * 0.3, s * 0.38), "pr_water_pale", parent,
              loc=(x - s * 0.35, y - s * 0.6, z + s * 0.25))
    elif kind == 3:
        # A little whirlwind: rings narrowing to a point.
        for k in range(4):
            r.add("orb_ring", TORUS(s * (1.05 - 0.24 * k), s * 0.22), ("pr_air_white", "pr_air_blue")[k % 2], parent,
                  loc=(x, y, z + s * (0.9 - 0.6 * k)))
    else:
        r.add("orb", E(s * 0.8, s * 0.8, s * 0.8), "pr_bolt_hot_glow", parent, loc=loc)
        for k in range(4):
            a = k * math.tau / 4 + 0.4
            d = Vector((math.cos(a), math.sin(a), 0.6 * (1 - 2 * (k % 2)))).normalized()
            bolt(r, parent, Vector(loc) + d * s * 0.6, Vector(loc) + d * s * 2.0, 2, 0.6, s * 0.22, "pr_bolt_glow",
                 120 + k)


def _molten_arm(r, side, sx, sh, flare):
    """An arm of molten rock: lumps of basalt over a core of magma that shows
    between them, a fist of basalt cracked with it, and small flames all
    along it -- a lot of little ones, because one big tongue of fire at this
    size is a striped cone."""
    names = []
    r.joint("shoulder_" + side, sh, "chest", rest=(0, -sx * flare, 0))
    r.add("pauldron", rock(0.18, 0.17, 0.13, 133, 0.10), "pr_basalt", "shoulder_" + side, loc=(sx * 0.02, 0.0, 0.05))
    crack(r, "shoulder_" + side, [(sx * -0.06, -0.15, 0.10), (sx * 0.04, -0.17, 0.05), (sx * 0.13, -0.13, 0.09)], 0.016)
    for j, length, seeds in (("shoulder_" + side, 0.32, (200, 201)), ("elbow_" + side, 0.29, (202, 203))):
        if j.startswith("elbow"):
            r.joint(j, (0, 0, -0.32), "shoulder_" + side, rest=(-14, 0, 0))
        r.add("magma", C(0.075, 0.065, length * 0.9), "pr_magma_glow", j, loc=(0, 0, -0.02))
        for k, s in enumerate(seeds):
            r.add("lump", rock(0.095, 0.09, length * 0.24, s, 0.12), "pr_basalt" if k else "pr_basalt_lt", j,
                  loc=(0.006 * (1 - 2 * k), 0.005, -length * (0.25 + 0.47 * k)))
        for k in range(2):
            z = -length * (0.30 + 0.40 * k)
            names.append(tongue(r, "fl_%s%d%s" % (j[:2], k, side), j, (sx * 0.05, 0.06, z),
                                (sx * 0.10, 0.15, z + 0.14), 0.055))
    r.joint("hand_" + side, (0, 0, -0.29), "elbow_" + side)
    r.add("fist", rock(0.15, 0.14, 0.14, 204, 0.12), "pr_basalt", "hand_" + side, loc=(0, -0.02, -0.09))
    for k in (-1, 0, 1):
        r.add("knuckle", rock(0.045, 0.04, 0.04, 205 + k), "pr_basalt_lt", "hand_" + side,
              loc=(k * 0.075, -0.14, -0.13))
    crack(r, "hand_" + side, [(-0.12, -0.12, -0.05), (-0.03, -0.15, -0.09), (0.05, -0.14, -0.04), (0.12, -0.11, -0.08)],
          0.018)
    for k in range(3):
        a = math.radians(-60 + k * 60)
        names.append(tongue(r, "fl_h%d%s" % (k, side), "hand_" + side, (math.sin(a) * 0.09, 0.04, -0.02),
                            (math.sin(a) * 0.15, 0.13, 0.13 + 0.03 * (k == 1)), 0.06))
    return names


def _stone_arm(r, side, sx, sh, flare):
    """An arm of boulders held together by amber, ending in a fist of rock,
    crystals grown out of the shoulder and the forearm."""
    r.joint("shoulder_" + side, sh, "chest", rest=(0, -sx * flare, 0))
    r.add("shoulder_stone", rock(0.19, 0.18, 0.17, 140, 0.10), "pr_stone_lt", "shoulder_" + side,
          loc=(sx * 0.02, 0, 0.03))
    r.add("upper", rock(0.12, 0.12, 0.15, 141, 0.14), "pr_stone", "shoulder_" + side, loc=(0, 0, -0.18))
    r.joint("elbow_" + side, (0, 0, -0.33), "shoulder_" + side, rest=(-12, 0, 0))
    mortar(r, "elbow_" + side, (0, 0, 0.0), 0.08, 0.08, 0.035)
    r.add("fore", rock(0.125, 0.12, 0.15, 142, 0.14), "pr_stone_dk", "elbow_" + side, loc=(0, 0, -0.15))
    r.joint("hand_" + side, (0, 0, -0.31), "elbow_" + side)
    mortar(r, "hand_" + side, (0, 0, 0.02), 0.07, 0.07, 0.03)
    r.add("fist", rock(0.17, 0.16, 0.16, 143, 0.12), "pr_stone", "hand_" + side, loc=(0, -0.02, -0.10))
    crack(r, "shoulder_" + side, [(sx * -0.05, -0.17, 0.08), (sx * 0.05, -0.18, 0.02), (sx * 0.15, -0.13, 0.06)],
          0.016, "pr_amber_glow")
    cluster(r, "shoulder_" + side, (sx * 0.06, 0.04, 0.15), (sx * 0.45, 0.2, 0.85), (0.30, 0.21, 0.17, 0.15), 0.06,
            (XTAL_G, XTAL_A), spread=30, turn=1.3)
    cluster(r, "elbow_" + side, (sx * 0.10, 0.04, -0.14), (sx * 0.9, 0.3, 0.2), (0.16, 0.11, 0.10), 0.045,
            (XTAL_A, XTAL_G), spread=28)


def build_quintessence():
    """All five elements in one body: a lower body of swirling water standing
    in a pool; a torso of dark stone with a radiant prismatic core in its
    chest, a seam of each element's light running out from it; one arm of
    molten fire and the other of boulder and crystal; a mantle of wind
    streamers off its shoulders; a crown of forked lightning; and five orbs,
    one of each element, going round it."""
    r = new_rig()
    r.joint("base", (0, 0, 0))
    pool(r, 0.44)
    puddle(r, 0.62)
    rubble(r, 0.40, 12, n=9)
    swirl_column(r, 0.04, 0.70, 0.15, 0.26, ("pr_water_lt", "pr_foam", "pr_water_pale", "pr_water_lt"), 0.06)
    names = []
    r.joint("body", (0, 0, 0))
    r.joint("pelvis", (0, 0, 0.70), "body")
    r.add("waist", E(0.25, 0.19, 0.12), "pr_water", "pelvis", loc=(0, 0, 0.0))
    r.add("hips", rock(0.24, 0.18, 0.10, 130, 0.10), "pr_quint", "pelvis", loc=(0, 0.01, 0.07))
    r.joint("chest", (0, 0, 0.08), "pelvis", rest=(6, 0, 0))
    r.add("torso", rock(0.37, 0.25, 0.33, 131, 0.07), "pr_quint", "chest", loc=(0, 0.02, 0.33))
    r.add("belly", rock(0.24, 0.18, 0.13, 132, 0.08), "pr_quint_lt", "chest", loc=(0, -0.02, 0.06))
    # The core: a white-hot heart in a rim of dark stone, a shard of each
    # element's light round it like the points of a star, and a seam of each
    # running out from it across the stone.
    r.add("core_rim", TORUS(0.13, 0.035), "pr_quint_dk", "chest", loc=(0, -0.215, 0.34), rot=(math.pi / 2, 0, 0))
    r.add("core_halo", E(0.12, 0.05, 0.12), "pr_prism_glow", "chest", loc=(0, -0.21, 0.34))
    r.add("core", E(0.075, 0.05, 0.075), "pr_prism_hot_glow", "chest", loc=(0, -0.245, 0.34))
    for k, col in enumerate(FIVE):
        a = math.radians(90 + k * 72)
        d = Vector((math.cos(a), 0, math.sin(a)))
        base = Vector((0, -0.24, 0.34)) + d * 0.05
        aim(r, r.add("ray", prism(0.026, 0.11, 4, 0.5), col, "chest", loc=tuple(base)), d + Vector((0, -0.4, 0)))
        crack(r, "chest", [tuple(Vector((0, -0.22, 0.34)) + d * 0.15),
                           tuple(Vector((0, -0.20, 0.34)) + d * 0.22 + Vector((0.02, 0, 0.02))),
                           tuple(Vector((0, -0.15, 0.34)) + d * 0.30 + Vector((-0.02, 0, 0.01)))], 0.015, col)
    # One arm molten fire, the other boulder and crystal.
    names += _molten_arm(r, "r", 1, (0.40, 0.03, 0.54), 8)
    _stone_arm(r, "l", -1, (-0.40, 0.03, 0.54), 8)
    # The mantle: streamers of wind off the shoulders and down the back to
    # the water, each on a joint of its own so it can wave.
    # (Fanned out as they fall and blown back off it in S-curves: hung
    # straight, from behind they were the bars of a cage.)
    r.joint("cape", (0, 0.18, 0.56), "chest", rest=(8, 0, 0))
    for k in range(7):
        f = (k - 3) / 3.0
        j = "wm%d" % k
        r.joint(j, (f * 0.28, 0.0, 0.02), "cape")
        pts = [Vector((f * 0.30 * g + 0.07 * math.sin(g * 5.0 + k * 1.3) * (0.3 + g),
                       0.06 + 0.34 * g + 0.05 * math.sin(g * 4 + k),
                       -0.60 * g + 0.10 * g * g + 0.04 * math.sin(g * 3 + k)))
               for g in (i / 7.0 for i in range(8))]
        chain(r, j, pts, 0.034, 0.012, ("pr_air_white", "pr_air_blue", "pr_air_violet")[k % 3], "streamer")
    r.joint("neck", (0, -0.03, 0.65), "chest")
    r.joint("head", (0, 0, 0.03), "neck", rest=(-14, 0, 0))
    r.add("skull", rock(0.17, 0.155, 0.165, 134, 0.06), "pr_quint", "head", loc=(0, 0, 0.12))
    r.add("face", rock(0.135, 0.065, 0.12, 135, 0.06), "pr_quint_lt", "head", loc=(0, -0.10, 0.11))
    r.add("brow", rock(0.17, 0.055, 0.04, 136, 0.08), "pr_quint_dk", "head", loc=(0, -0.145, 0.19))
    for sx in (-1, 1):
        r.add("eye", E(0.05, 0.022, 0.033), "pr_prism_hot_glow", "head", loc=(sx * 0.07, -0.168, 0.13))
    # The crown: forked lightning, two ways of it flickering between.
    crown = []
    for c in range(2):
        j = "crown%d" % c
        r.joint(j, (0, 0, 0), "head")
        for k in range(5):
            a = math.radians(-150 + k * 60)
            root = Vector((math.cos(a) * 0.11, math.sin(a) * 0.09 + 0.02, 0.23))
            tip = root + Vector((math.cos(a) * 0.09, math.sin(a) * 0.07, 0.26 + 0.05 * (k % 2) - 0.04 * abs(k - 2)))
            bolt(r, j, root, tip, 3, 0.5, 0.026, "pr_bolt_hot_glow" if (k + c) % 2 else "pr_bolt_glow", 150 + k + 7 * c,
                 fork=(0.6, tip + Vector((math.cos(a + 0.9) * 0.08, math.sin(a + 0.9) * 0.06, -0.02))))
        if c:
            r.j[j].scale = (0.01, 0.01, 0.01)
        crown.append(j)
    QUINT["crown"][:] = crown
    # The orbs: one of each element, going round it on a tilted ring --
    # tilted on one joint, turned on another inside it.
    r.joint("orbs", (0, 0, 1.08), "base", rest=(12, -6, 0))
    r.joint("spin", (0, 0, 0), "orbs")
    for k in range(5):
        a = k * math.tau / 5
        _orb(r, "spin", (math.cos(a) * 0.70, math.sin(a) * 0.70, 0.0), k, 0.065)
    QUINT["tongues"][:] = names
    quint_bits(r)
    return r.fit("quintessence", 100, quint_idle(0.0))


# What it comes apart into: the orbs flung off, embers off the fire arm,
# stones off the other, a splash from the water, wisps of the wind.
# (The orbs are swapped for these at t = 0.2, when the ring has turned a fifth:
# each starts where its orb is then, not where it began, or they trade colours.)
QUINT_ORBS = [((math.cos(a) * 0.70, math.sin(a) * 0.70, 1.08), (math.cos(a) * 0.9, math.sin(a) * 0.8, 0.9))
              for a in ((k + 1) * math.tau / 5 for k in range(5))]
# (Thrown no further than this: out to the side, in the side rows the camera
# sees a throw sideways as one towards it, and the furthest landed on the
# bottom edge of the frame.)
QUINT_EMBERS = burst([(0.30, -0.10), (0.36, 0.04), (0.24, -0.16), (0.40, -0.06)], 1.10, 0.62, 0.7)
QUINT_STONES = burst([(-0.30, -0.10), (-0.36, 0.04), (-0.24, -0.16), (-0.40, -0.06), (-0.20, 0.10)], 1.05, 0.58, 0.5)
QUINT_DROPS = burst([(-0.16, -0.16), (0.16, -0.16), (0.0, -0.22), (-0.22, 0.06), (0.22, 0.06), (0.0, 0.16)], 0.70,
                    0.70, 1.3)
QUINT_WISPS = [((math.cos(a) * 0.20, 0.25 + math.sin(a) * 0.10, 1.10),
                (math.cos(a) * 0.7, 0.4 + math.sin(a) * 0.4, 0.4))
               for a in (k * math.tau / 5 + 0.3 for k in range(5))]


def quint_bits(r):
    orbs = Bits(r, "eorb")
    for i, (p0, _) in enumerate(QUINT_ORBS):
        orbs.add(p0, E(0.06, 0.06, 0.06), FIVE[i])
    embers = Bits(r, "eplate")
    for i, (p0, _) in enumerate(QUINT_EMBERS):
        embers.add(p0, rock(0.06, 0.05, 0.05, 160 + i), "pr_basalt" if i % 2 else "pr_ember_glow")
    stones = Bits(r, "estone")
    for i, (p0, _) in enumerate(QUINT_STONES):
        stones.add(p0, rock(0.075, 0.07, 0.065, 170 + i), ("pr_stone", "pr_stone_dk")[i % 2] if i < 4 else "pr_xtal_g")
    drops = Bits(r, "edrop")
    for i, (p0, _) in enumerate(QUINT_DROPS):
        drops.add(p0, E(0.04, 0.04, 0.05), ("pr_water_lt", "pr_foam", "pr_water")[i % 3])
    wisps = Bits(r, "ewisp")
    for i, (p0, _) in enumerate(QUINT_WISPS):
        wisps.add(p0, C(0.045, 0.015, 0.20, squash_y=0.6), AIR[i % 3], rot=(math.pi / 2, 0, i))
    discharge(r, "flash", (0, -0.10, 1.12), 9, 0.86, 180, width=0.036)


QUINT_REST = {"shoulder_l": (-10, 0, 3), "shoulder_r": (-8, 0, 0), "elbow_l": X(-16), "elbow_r": X(-22),
              "hand_l": X(-8), "hand_r": X(-6)}
# Both arms up -- the fire and the stone -- the orbs flung wide, the crown
# flaring...
QUINT_RAISE = {"chest": X(-16), "head": X(-8), "_y": 0.05, "_z": 0.03,
               "shoulder_l": (-160, 0, 18), "shoulder_r": (-160, 0, -16), "elbow_l": X(-28), "elbow_r": X(-30),
               "hand_l": X(-10), "hand_r": X(-10), "cape": X(-16)}
# ...and both brought down together on whatever is in front of it.
QUINT_SLAM = {"chest": X(26), "head": X(-6), "_y": -0.14, "_z": -0.03,
              "shoulder_l": (-80, 0, 12), "shoulder_r": (-80, 0, -12), "elbow_l": X(-6), "elbow_r": X(-6),
              "hand_l": X(10), "hand_r": X(10), "cape": X(14)}


def _quint_common(v, t, speed=1.0, lean=0.0, wave=1.0):
    """What is always going on: the water turning under it, the orbs going
    round (a whole turn a loop: they are five different things, so only a
    whole turn brings each back to its own place), the fire on its arm
    flickering, the crown crackling, the mantle waving."""
    v["swirl"] = (0, 0, 90.0 * t * speed)
    v["spin"] = (0, 0, 360.0 * t)
    for k in range(7):
        v["wm%d" % k] = (lean + 8 * wave * sn(t, k * 0.14), 0, 6 * wave * sn(t, 0.3 + k * 0.11))
    crackle(v, t, QUINT["crown"], 1, 3)
    return v


def quint_idle(t):
    s = sn(t)
    v = dict(QUINT_REST)
    v.update({"_z": 0.010 * s, "chest": X(2 * s), "head": (0, 0, 4 * sn(t, 0.3)), "pelvis": (0, 0, 4 * s),
              "cape": X(3 * s), "shoulder_l": (-10 + 4 * s, 0, 3), "shoulder_r": (-8 - 4 * s, 0, 0)})
    _quint_common(v, t)
    return flicker(v, QUINT["tongues"], t)


def quint_walk(t):
    s = sn(t)
    v = dict(QUINT_REST)
    v.update({"_z": 0.016 * abs(s), "chest": X(10 + 2 * s), "pelvis": (0, 0, 5 * s), "head": (0, 0, -4 * s),
              "cape": (-18 - 4 * sn(t * 2), 0, 3 * s), "shoulder_l": (-10 + 12 * s, 0, 3),
              "shoulder_r": (-8 - 12 * s, 0, 0)})
    _quint_common(v, t, 2.0, -14.0, 1.4)
    return flicker(v, QUINT["tongues"], t, amp=11.0, swell=0.2, lean=(-16.0, 0.0))


def quint_attack(t):
    v = swing(t, QUINT_REST, QUINT_RAISE, QUINT_SLAM, (0.44, 0.60, 1.0))
    i, k = phases(t, 0.44, 0.60, 1.0)
    flare = [k, 1.0, 1.0 - k, 0.0][i]
    _quint_common(v, t, 2.0)
    v["~orbs"] = 0.25 * flare
    crackle(v, t, QUINT["crown"], 1 + (i == 1), 3)
    return flicker(v, QUINT["tongues"], t, amp=10.0, swell=0.18, speed=2, grow=0.35 * flare)


def quint_hurt(t):
    k = math.sin(t * math.pi)
    v = dict(QUINT_REST)
    v.update(struck(k * 0.8, shoulder_l=(-30, 0, 20), shoulder_r=(-30, 0, -20), cape=X(-16)))
    _quint_common(v, t)
    v["~orbs"] = 0.15 * k
    return flicker(v, QUINT["tongues"], t, amp=14.0, swell=0.2, speed=2, grow=-0.3 * k)


def quint_death(t):
    # It comes apart into all five at once. The core flares and the
    # lightning goes out of it into the ground; the orbs are flung off; the
    # fire arm sheds its embers and the stone arm its stones; the wind is
    # torn off it in wisps; and the water falls in on itself in a splash and
    # spreads -- leaving a puddle with rubble in it, on scorched ground.
    flare = ease(min(1.0, t / 0.2)) * (1.0 - ease(min(1.0, max(0.0, (t - 0.3) / 0.3))))
    fall = ease(min(1.0, max(0.0, (t - 0.3) / 0.5)))
    v = dict(QUINT_REST)
    v.update({"chest": X(-14 * flare + 26 * fall), "head": X(-12 * flare + 18 * fall), "_z": 0.04 * flare,
              "shoulder_l": (-10 - 50 * flare, 0, 3 + 40 * flare), "shoulder_r": (-8 - 50 * flare, 0, -40 * flare),
              "~body": -0.99 * fall, "~swirl": -0.99 * ease(min(1.0, max(0.0, (t - 0.45) / 0.4))),
              "~cape": -0.99 * ease(min(1.0, max(0.0, (t - 0.2) / 0.3)))})
    _quint_common(v, t, 3.0)
    flicker(v, QUINT["tongues"], t, amp=12.0, swell=0.2, speed=2, grow=0.45 * flare)
    gutter(v, QUINT["tongues"], ease(min(1.0, max(0.0, (t - 0.3) / 0.4))))
    v["~orbs"] = -0.99 if t >= 0.2 else 0.3 * flare
    if t > 0.45:
        for j in QUINT["crown"]:
            v["~" + j] = -1.0
    v["~flash"] = 0.0 if 0.1 < t < 0.5 else -1.0
    v["~scorch"] = 0.0 if t > 0.15 else -1.0
    spread = ease(min(1.0, max(0.0, (t - 0.5) / 0.45)))
    v["~puddle"] = -0.99 + 0.99 * spread
    v["~pool"] = -0.99 * spread
    v["~rubble"] = -0.99 + 0.99 * ease(min(1.0, max(0.0, (t - 0.55) / 0.4)))
    if t >= 0.2:
        b = (t - 0.2) / 0.8
        for i, (p0, vel) in enumerate(QUINT_ORBS):
            p = Vector(p0) + Vector(vel) * b
            put(v, "eorb", i, p - Vector(p0), max(0.0, 1.0 - b * 1.3))
        for i, (p0, d) in enumerate(QUINT_WISPS):
            put(v, "ewisp", i, Vector(d) * b, max(0.0, 1.0 - b * 1.2), (0, 0, 90 * b))
    if t >= 0.35:
        b = (t - 0.35) * 1.8
        for i, (p0, vel) in enumerate(QUINT_EMBERS):
            p = thrown(p0, vel, b, grav=3.0, floor=0.04)
            put(v, "eplate", i, p - Vector(p0), 1.0, (150 * b, 90 * b, 0))
        for i, (p0, vel) in enumerate(QUINT_STONES):
            p = thrown(p0, vel, b, grav=3.0, floor=0.05)
            put(v, "estone", i, p - Vector(p0), 1.0, (120 * b, 60 * b, 0))
        for i, (p0, vel) in enumerate(QUINT_DROPS):
            p = thrown(p0, vel, b * 1.2, grav=4.0, floor=0.03)
            put(v, "edrop", i, p - Vector(p0), 0.0 if (p.z <= 0.031 and t > 0.8) else 1.0)
    return v


# =================================================================================
#  Registration
# =================================================================================

# id: (builder, frame px, (idle, walk, attack, hurt, death), shadow radius)
CREATURES = {
    "ember_conjure": (build_ember_conjure, 112,
                      (ember_idle, ember_walk, ember_attack, ember_hurt, ember_death), 0.52),
    "inferno_conjure": (build_inferno_conjure, 144,
                        (inferno_idle, inferno_walk, inferno_attack, inferno_hurt, inferno_death), 0.68),
    "stone_conjure": (build_stone_conjure, 112,
                      (stone_idle, stone_walk, stone_attack, stone_hurt, stone_death), 0.56),
    "monolith_conjure": (build_monolith_conjure, 144,
                         (monolith_idle, monolith_walk, monolith_attack, monolith_hurt, monolith_death), 0.72),
    "tide_conjure": (build_tide_conjure, 112, (tide_idle, tide_walk, tide_attack, tide_hurt, tide_death), 0.48),
    "maelstrom_conjure": (build_maelstrom_conjure, 144, (mael_idle, mael_walk, mael_attack, mael_hurt, mael_death),
                          0.66),
    "gale_conjure": (build_gale_conjure, 112, (gale_idle, gale_walk, gale_attack, gale_hurt, gale_death), 0.40),
    "cyclone_conjure": (build_cyclone_conjure, 144,
                        (cyclone_idle, cyclone_walk, cyclone_attack, cyclone_hurt, cyclone_death), 0.60),
    "storm_conjure": (build_storm_conjure, 112, (storm_idle, storm_walk, storm_attack, storm_hurt, storm_death), 0.40),
    "thunder_conjure": (build_thunder_conjure, 144,
                        (thunder_idle, thunder_walk, thunder_attack, thunder_hurt, thunder_death), 0.56),
    "quintessence": (build_quintessence, 192, (quint_idle, quint_walk, quint_attack, quint_hurt, quint_death), 0.90),
}


def register():
    cr.CREATURES.update(CREATURES)
