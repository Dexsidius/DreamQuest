"""Turns the Bayou guide drawn in LevelEdit-Plus into C++ tables for genmaps.

The guide is exports/Bayou/Bayou.mx in the LevelEdit-Plus tree. Nothing of its
art is used -- it is a map of intentions: where the track runs, which rings are
water, where the decks stand, where the stakes go, where the flowers and the
monsters are. Positions are moved into the game's frame (the north-west bush at
one cell in from the corner) and emitted as literal tables, so the map does not
depend on a file outside the repo and every number can be traced back.
"""
import json, math, io

import sys
# python tools/bayou_guide.py <the guide's Bayou.mx> <where the tables go>
GUIDE = sys.argv[1] if len(sys.argv) > 1 else r'E:\Claude\LevelEdit-Plus-dexsidius-dev\exports\Bayou\Bayou.mx'
OX, OY = 5585, 238              # guide -> game: the NW bush lands one cell in
CELL = 32
W, H = 226, 88                  # cells: the bushes' box and a wall round it

d = json.load(open(GUIDE, encoding='utf-8'))['tiles']


def pts(name):
    # The guide's positions are the tile's top-left; the middle is what was meant.
    return [(l[0] + 16 + OX, l[1] + 16 + OY) for l in d[name]['locations']]


def clamp_pt(x, y, m=48):
    return (min(max(x, m), W * CELL - m), min(max(y, m), H * CELL - m))


def g(x, y):
    """A guide point, in the game frame."""
    return (x + 16 + OX, y + 16 + OY)


# --- water ---------------------------------------------------------------------------------
# Each ring of puddles is the shore of one body of water. Grouped by the box it
# was drawn in, ordered round its middle into a polygon.
LAKES = [
    ('entrance', (295, 1244, 70, 290)),      # a crescent: closed along the north edge
    ('southeast', (550, 1420, 2070, 2430)),
    ('village_n', (-3850, -2375, -250, 405)),
    ('pond_b', (-2925, -2505, 985, 1340)),
    ('pond_a', (-4460, -3725, 985, 1485)),
    ('glade', (-5290, -4800, 180, 675)),
    ('pond_sw', (-2385, -1885, 2130, 2485)),
    ('village_s', (-5240, -3040, 1840, 2470)),
]
puddles = [(l[0], l[1]) for l in d['Huge puddle']['locations']]
lakes = {}
for name, (x0, x1, y0, y1) in LAKES:
    ring = [p for p in puddles if x0 - 10 <= p[0] <= x1 + 10 and y0 - 10 <= p[1] <= y1 + 10]
    ring = [g(*p) for p in ring]
    if name == 'entrance':
        # The arc is the south shore; the lake runs up to the north wall.
        xs = [p[0] for p in ring]
        ring += [(max(xs) + 36, 40), (min(xs) - 36, 40)]
    cx = sum(p[0] for p in ring) / len(ring)
    cy = sum(p[1] for p in ring) / len(ring)
    ring.sort(key=lambda p: math.atan2(p[1] - cy, p[0] - cx))
    # The markers sit on the shore; the water comes up to them, not short.
    grown = []
    for (x, y) in ring:
        dx, dy = x - cx, y - cy
        k = (math.hypot(dx, dy) + 18) / max(1, math.hypot(dx, dy))
        grown.append(clamp_pt(int(cx + dx * k), int(cy + dy * k), 20))
    lakes[name] = grown
used = sum(len(v) for v in lakes.values())
assert used >= len(puddles) - 2, (used, len(puddles))

# --- decks ---------------------------------------------------------------------------------
# Four pillars to a deck, at its corners.
pillars = [(l[0], l[1]) for l in d['pac_vertical']['locations']]


def deck(xs, ys):
    ps = [p for p in pillars if xs[0] <= p[0] <= xs[1] and ys[0] <= p[1] <= ys[1]]
    assert len(ps) == 4, ps
    x0 = min(p[0] for p in ps); x1 = max(p[0] for p in ps) + 32
    y0 = min(p[1] for p in ps); y1 = max(p[1] for p in ps) + 32
    a, b = g(x0 - 16, y0 - 16), g(x1 - 16, y1 - 16)
    return (a[0] // CELL, a[1] // CELL, (b[0] + CELL - 1) // CELL, (b[1] + CELL - 1) // CELL)


DECKS = {
    'n_west': deck((-3700, -3200), (-150, 250)),
    'n_east': deck((-3000, -2400), (-150, 250)),
    's_west': deck((-5100, -4500), (1990, 2300)),
    's_east': deck((-4150, -3700), (2030, 2330)),
}
# The ramps the guide drew rise toward the north decks; the south village has
# none drawn, and is walked up from the north shore, which is where the land is.
poles = [(l[0], l[1], l[4]) for l in d['pac_poleender']['locations']]
ramp_cols = sorted({round((p[0] + 16 + OX) / CELL) for p in poles})

# --- the stakes ------------------------------------------------------------------------------
walls = [(l[0], l[1]) for l in d['Medium ground']['locations']]


def ring_of(box):
    x0, x1, y0, y1 = box
    ps = [g(*p) for p in walls if x0 <= p[0] <= x1 and y0 <= p[1] <= y1]
    cx = sum(p[0] for p in ps) / len(ps)
    cy = sum(p[1] for p in ps) / len(ps)
    ps.sort(key=lambda p: math.atan2(p[1] - cy, p[0] - cx))
    return ps, (int(cx), int(cy))


camp_n, camp_n_mid = ring_of((-1800, -900, 80, 470))
camp_c, camp_c_mid = ring_of((-1120, 360, 880, 1580))
assert len(camp_n) + len(camp_c) == len(walls), (len(camp_n), len(camp_c), len(walls))

# --- the track ---------------------------------------------------------------------------
# The guide's dark ground, in the order it is walked.
P = {tuple(p) for p in [(l[0], l[1]) for l in d['Dark Ground']['locations']]}
ENTRY = [(1568, 80), (1537, 110), (1502, 141), (1464, 179), (1430, 209), (1391, 245), (1353, 280), (1306, 315),
         (1263, 348), (1212, 389), (1151, 425), (1120, 460), (1058, 543), (940, 617), (873, 700)]
LOOP = [(873, 700), (713, 656), (482, 629), (168, 634), (-3, 667), (-210, 680), (-398, 627), (-557, 604),
        (-720, 587), (-911, 597), (-1156, 616), (-1352, 694), (-1462, 737), (-1625, 799), (-1715, 859),
        (-1778, 959), (-1815, 1078), (-1846, 1171), (-1867, 1287), (-1828, 1422), (-1779, 1549), (-1722, 1710),
        (-1633, 1818), (-1546, 1898), (-1431, 1981), (-1178, 1975), (-930, 1972), (-648, 1972), (-302, 1976),
        (33, 1976), (295, 1974), (519, 1905), (595, 1806), (661, 1642), (634, 1486), (668, 1326), (659, 1132),
        (661, 937), (629, 830), (701, 749), (873, 700)]
WEST = [(-1715, 859), (-1841, 879), (-2061, 878), (-2208, 877), (-2283, 876), (-2498, 869), (-2700, 868),
        (-2913, 870), (-3103, 869), (-3306, 864), (-3504, 859), (-3704, 867), (-3926, 872), (-4141, 867),
        (-4343, 869), (-4563, 849), (-4715, 829), (-4836, 835), (-5053, 843), (-5282, 863)]
SPUR = [(-4715, 829), (-4714, 671), (-4711, 556), (-4725, 363), (-4756, 222), (-4781, 110), (-4791, 15),
        (-4790, -110), (-4787, -200)]
for line in (ENTRY, LOOP, WEST, SPUR):
    for p in line:
        assert p in P, p
assert len(P - set(ENTRY) - set(LOOP) - set(WEST) - set(SPUR)) == 0
# And the ways in that the guide leaves to be walked: into each camp's gate, up
# to the foot of each north ramp, and down to the south village's shore.
N_GATE = (-1368, 460); C_GATE_N = (-380, 893); C_GATE_S = (-313, 1563)
SPURS = [
    [(-1352, 694), (-1368, 600), N_GATE],
    [(-398, 627), (-380, 760), C_GATE_N],
    [(-302, 1976), (-313, 1760), C_GATE_S],
    [(-3564, 864), (-3564, 700), (-3564, 470)],
    [(-2828, 870), (-2828, 700), (-2828, 470)],
    [(-3306, 864), (-3330, 1150), (-3420, 1520), (-3700, 1700), (-3930, 1790)],
]

# --- flowers, dark grass -------------------------------------------------------------------
flowers = [clamp_pt(*g(*p)) for p in [(l[0], l[1]) for l in d['Flower ground piece']['locations']]]
dark = [clamp_pt(*g(*p)) for p in [(l[0], l[1]) for l in d['Dark Grass Patch']['locations']]]


# --- who lives where -------------------------------------------------------------------------
# Every icon the guide put down, given the monster the place asks for. `lurk`
# is a post under the water. Levels are the spawn level: 1 is the monster's own.
def icons(name):
    return [(l[0], l[1]) for l in d[name]['locations']]


def near(p, q, r=4):
    return abs(p[0] - q[0]) <= r and abs(p[1] - q[1]) <= r


posts = []   # (type, gx, gy, level, lurk, pool)


def inside(poly, x, y):
    hit = False
    j = len(poly) - 1
    for i in range(len(poly)):
        (xi, yi), (xj, yj) = poly[i], poly[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            hit = not hit
        j = i
    return hit


def settle(x, y):
    """A post under the water has to be in the water. An icon drawn on the
    shore line, or a pixel past a ring's tip, is walked toward the middle of
    the nearest lake until it is a cell inside."""
    best = min(lakes.values(), key=lambda poly: min(math.hypot(px - x, py - y) for px, py in poly))
    cx = sum(p[0] for p in best) / len(best)
    cy = sum(p[1] for p in best) / len(best)
    for _ in range(80):
        if inside(best, x, y) and all(inside(best, x + ox, y + oy) for ox, oy in ((32, 0), (-32, 0), (0, 32), (0, -32))):
            return int(x), int(y)
        d = math.hypot(cx - x, cy - y) or 1
        x, y = x + (cx - x) / d * 12, y + (cy - y) / d * 12
    raise SystemExit('could not put a post in the water at %s' % ((x, y),))


def post(t, p, lv=1, lurk=False, pool=None):
    x, y = clamp_pt(*g(*p), 56)
    if lurk:
        x, y = settle(x, y)
    posts.append((t, x, y, lv, lurk, pool))


E, E2, E3 = icons('enemy'), icons('enemy2'), icons('enemy3')
claimed = set()


def take(src, p):
    for q in src:
        if near(q, p):
            claimed.add(q)
            return q
    raise SystemExit('no icon at %s' % (p,))


# The crescent by the way in: things under the water, and croakers on its bank.
for p in [(386, 132), (239, 143), (841, 152), (1125, 105)]:
    post('bog_lurker', take(E, p), 1 if p != (841, 152) else 2, lurk=True)
for p in [(620, 374), (961, 368)]:
    post('mire_croaker', take(E, p))
# The south-east lake.
for p in [(1294, 2179), (941, 2199), (666, 2308)]:
    post('fen_gator', take(E, p), lurk=True)
post('mire_croaker', take(E, (555, 2170)))
post('mire_croaker', take(E, (933, 2063)))
post('fen_stalker', take(E, (1244, 2019)))
post('rot_shambler', take(E, (247, 2282)))
# The ring round the big camp: whoever keeps the ground outside the stakes.
RING = [(-572, 1660), (-97, 1697), (-1077, 1646), (-1223, 1379), (-1193, 1221), (-1119, 1017), (-1018, 901),
        (-1265, 1110), (-1336, 1356), (-1287, 1634), (-839, 1755), (199, 1662), (389, 1569), (451, 1366),
        (439, 1118), (403, 916), (149, 784), (-93, 765), (-644, 770), (-876, 758)]
for i, p in enumerate(RING):
    post('rot_shambler', take(E, p), 1 + (i % 2), pool=['rot_shambler', 'bog_lurker', 'rot_shambler'])
# The north camp: its hags and its shamans, and lizardmen on the gate.
NCAMP = [(-1597, 177), (-1635, 232), (-1600, 306), (-1224, 138), (-1101, 178), (-1088, 251), (-1192, 304),
         (-1272, 312)]
for i, p in enumerate(NCAMP):
    post('lizard_shaman' if i in (0, 3, 6) else 'swamp_hag', take(E2, p), 1 if i in (0, 3, 6) else 2)
for p in [(-1473, 207), (-1444, 274), (-1291, 203), (-1310, 281), (-1387, 370)]:
    post('bog_lurker', take(E, p), 3)
# The big camp.
CCAMP = [(-496, 932), (-255, 934), (-457, 1446), (-188, 1448), (-488, 1166), (-469, 1290), (44, 1031),
         (85, 1294), (-828, 1255), (-746, 1072), (-739, 1390)]
for i, p in enumerate(CCAMP):
    post('lizard_shaman' if i in (4, 7, 9) else 'swamp_hag', take(E3, p), 1 if i in (4, 7, 9) else 2)
for p in [(189, 1150), (-867, 1154)]:
    post('lizard_shaman', take(E2, p), 1)
# The ponds by the west road: the drowned, and a gator.
for i, p in enumerate([(-2844, 1190), (-2744, 1087), (-2612, 1193), (-2765, 1271)]):
    post('fen_gator' if i == 2 else 'drowned_one', take(E, p), 1, lurk=True)
post('drowned_one', take(E, (-4279, 1139)), 1, lurk=True)
post('fen_gator', take(E, (-4279, 1288)), 1, lurk=True)
post('bog_lurker', take(E, (-3932, 1317)), 3, lurk=True)
# The witch ring at the loop's south-west corner: lights, and the dead they
# keep dancing.
WRING = [(-1968, 2039), (-1951, 2078), (-1893, 2093), (-1856, 2056), (-1843, 2002), (-1879, 1973), (-1928, 1985),
         (-1955, 2011)]
for i, p in enumerate(WRING):
    post('witchlight' if i % 2 == 0 else 'rot_shambler', take(E3, p), 1 if i % 2 == 0 else 2)
# The north village.
post('drowned_one', take(E3, (-3674, 311)), 1, lurk=True)
post('bog_lurker', take(E3, (-2901, 390)), 3, lurk=True)
post('swamp_hag', take(E3, (-3750, 536)), 2)
post('swamp_hag', take(E3, (-3220, 553)), 2)
post('mire_croaker', take(E3, (-2335, 302)), 2)
for p in [(-3894, 179), (-3909, -42), (-3941, -211)]:
    post('witchlight', take(E3, p), 1)
# The south village: gators under its east end, the drowned under its decks,
# a croaker on its shore and a lurker on its east bank.
post('fen_gator', take(E2, (-3612, 2316)), 2, lurk=True)
post('fen_gator', take(E2, (-3219, 2341)), 2, lurk=True)
post('mire_croaker', take(E2, (-3289, 1864)), 2)
post('bog_lurker', take(E2, (-3041, 2053)), 4)
# The four drawn along the south edge are under the water south of the decks,
# where they can reach somebody on the boards: the strip of bank behind the
# lake is a cell or two wide, and nobody goes there.
for p in [(-3811, 2549), (-4242, 2531), (-4674, 2518), (-5134, 2518)]:
    q = take(E2, p)
    post('drowned_one', (q[0], 2400), 2, lurk=True)
assert claimed == set(E) | set(E2) | set(E3), (len(claimed), len(E) + len(E2) + len(E3))

# The fen stalkers in the east thicket, where the guide drew dark growth and no
# icons: spiders are what lives in undergrowth nobody walks.
for p in [(1201, 1158), (1342, 1448), (1296, 1789), (1362, 730)]:
    post('fen_stalker', p, 1)


# --- emit ------------------------------------------------------------------------------------
def cpp_points(ps):
    return ', '.join('{%d, %d}' % (int(x), int(y)) for x, y in ps)


out = []
out.append('// ---- generated from the Bayou guide by tools/bayou_guide.py: edit that, not this ----')
out.append('static const int BY_W = %d, BY_H = %d, BY_CELL = %d;' % (W, H, CELL))
for name, ring in lakes.items():
    out.append('static const std::vector<std::pair<int, int>> kBayouLake_%s = {%s};' % (name, cpp_points(ring)))
out.append('static const std::vector<const std::vector<std::pair<int, int>>*> kBayouLakes = {%s};'
           % ', '.join('&kBayouLake_%s' % n for n in lakes))
for name, (x0, y0, x1, y1) in DECKS.items():
    out.append('static const int kDeck_%s[4] = {%d, %d, %d, %d};   // cells: x0, y0, x1, y1 (exclusive)'
               % (name, x0, y0, x1, y1))
out.append('static const int kRampCols[2] = {%d, %d};   // the north decks\' ramps, by cell column' % tuple(ramp_cols))
out.append('static const std::vector<std::pair<int, int>> kCampNorth = {%s};' % cpp_points(camp_n))
out.append('static const std::vector<std::pair<int, int>> kCampMid = {%s};' % cpp_points(camp_c))
out.append('static const int kCampNorthMid[2] = {%d, %d}, kCampMidMid[2] = {%d, %d};'
           % (camp_n_mid + camp_c_mid))
for name, line in (('Entry', ENTRY), ('Loop', LOOP), ('West', WEST), ('Spur', SPUR)):
    out.append('static const std::vector<std::pair<int, int>> kTrack%s = {%s};' % (name, cpp_points([g(*p) for p in line])))
for i, line in enumerate(SPURS):
    out.append('static const std::vector<std::pair<int, int>> kTrackWay%d = {%s};' % (i, cpp_points([g(*p) for p in line])))
out.append('static const std::vector<const std::vector<std::pair<int, int>>*> kBayouTracks = {'
           '&kTrackEntry, &kTrackLoop, &kTrackWest, &kTrackSpur, %s};'
           % ', '.join('&kTrackWay%d' % i for i in range(len(SPURS))))
out.append('static const std::vector<std::pair<int, int>> kBayouFlowers = {%s};' % cpp_points(flowers))
out.append('static const std::vector<std::pair<int, int>> kBayouDark = {%s};' % cpp_points(dark))
out.append('struct BayouPost { const char* type; int x, y, level; bool lurk; const char* pool[3]; };')
rows = []
for (t, x, y, lv, lurk, pool) in posts:
    pl = ', '.join('"%s"' % p for p in pool) if pool else 'nullptr, nullptr, nullptr'
    rows.append('    {"%s", %d, %d, %d, %s, {%s}},' % (t, x, y, lv, 'true' if lurk else 'false', pl))
out.append('static const BayouPost kBayouPosts[] = {\n%s\n};' % '\n'.join(rows))
out.append('// ---- end of the generated tables ----')
io.open(sys.argv[2] if len(sys.argv) > 2 else 'bayou_tables.inc',
        'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')

print('lakes:', {k: len(v) for k, v in lakes.items()})
print('decks (cells):', DECKS)
print('north ramps at columns', ramp_cols)
print('camps: north %d stakes, middle %d stakes' % (len(camp_n), len(camp_c)))
print('posts: %d' % len(posts))
import collections
print(collections.Counter(p[0] for p in posts))
print('lurking: %d' % sum(1 for p in posts if p[4]))
