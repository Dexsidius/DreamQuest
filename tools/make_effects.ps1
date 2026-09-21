# =============================================================================
#  make_effects.ps1 - draws what a spell looks like in the air.
#
#      .\tools\make_effects.ps1
#
#  The four elements used to be thrown as whatever icon was nearest: fire was
#  the spark from the tinderbox, water a blue gem, earth a small rock from the
#  scenery and air a rune, each of them eleven pixels across and turned or spun
#  as it flew. They could be told apart by colour and not by anything else.
#
#  So each is drawn here as the thing it is, as a strip of frames read left to
#  right, pointing along +x (the game turns it to the way it is going):
#
#    fireball(_greater)     a round white-hot head and a tail of flame streaming
#                           back off it
#    water_orb(_greater)    a ball of water, upright, that wobbles as it flies:
#                           dark rim, a highlight top-left, the light through it
#                           bottom-right
#    water_wake(_greater)   what streams off the back of it; drawn under the orb
#                           and turned to the way it is going
#    rock_shard(_greater)   a faceted stone, tumbling, lit from the top-left in
#                           every frame (which is why it is frames and not one
#                           picture spun: a spun picture turns its own shadow)
#    gust(_greater)         lines of moving air that curl over at the front; the
#                           greater one has an edge on it
#    glow                   a soft round light, added under what burns
#    acid_glob, acid_wake   the Acid Spray's gouts: the water orb and its wake, in green
#    blood_orb, blood_wake  the Vampiric Touch: the same, in red
#    frost_shard            the Ice Touch: the stone shard, cut in ice
#    throwing_knife         a knife going end over end
#    air_slash              the Air Slash: the greater gust's edge, with nothing behind it
#
#  Flame, the wake and the gust are fields and not drawings: a number is worked
#  out for every pixel -- how hot, how wet -- and cut into four or five flat
#  colours, which is what makes it pixel art and not a blur. What scrolls
#  through them is noise that repeats, so the last frame runs into the first.
#
#  The sizes and where each is held (its pivot, the point that is on the
#  projectile's own position and that it is turned about) are written out at
#  the end, for data/projectiles.json.
# =============================================================================

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$out  = Join-Path $root "assets\effects"
New-Item -ItemType Directory -Force -Path $out | Out-Null

$source = @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;

public static class Fx
{
    // ------------------------------------------------------------------ noise
    static double Hash(int x, int y, int seed)
    {
        unchecked {
            uint h = (uint)(x * 374761393) + (uint)(y * 668265263) + (uint)(seed * 982451653);
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= h >> 16;
            return (h & 0xFFFFFF) / (double)0x1000000;
        }
    }
    static double Smooth(double t) { return t * t * (3 - 2 * t); }
    static int Mod(int a, int m) { int r = a % m; return r < 0 ? r + m : r; }

    // Value noise that repeats every `period` cells along x, so that something
    // scrolled through it by a whole period is back where it began.
    static double Noise(double x, double y, int period, int seed)
    {
        int x0 = (int)Math.Floor(x), y0 = (int)Math.Floor(y);
        double fx = Smooth(x - x0), fy = Smooth(y - y0);
        int xa = Mod(x0, period), xb = Mod(x0 + 1, period);
        double a = Hash(xa, y0, seed), b = Hash(xb, y0, seed);
        double c = Hash(xa, y0 + 1, seed), d = Hash(xb, y0 + 1, seed);
        return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
    }
    static double Fbm(double x, double y, int period, int seed)
    {
        return Noise(x, y, period, seed) * 0.65 + Noise(x * 2, y * 2, period * 2, seed + 7) * 0.35;
    }

    // ---------------------------------------------------------------- helpers
    static Bitmap Blank(int w, int h)
    {
        Bitmap b = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        using (Graphics g = Graphics.FromImage(b)) g.Clear(Color.Transparent);
        return b;
    }

    // The first colour whose stop the value reaches; nothing, below them all.
    static Color Ramp(double v, double[] stops, Color[] cols)
    {
        for (int i = 0; i < stops.Length; ++i) if (v >= stops[i]) return cols[i];
        return Color.Transparent;
    }

    // A line of `c` round everything drawn, inside the frame.
    static void Outline(Bitmap b, Color c)
    {
        int w = b.Width, h = b.Height;
        bool[,] solid = new bool[w, h];
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) solid[x, y] = b.GetPixel(x, y).A >= 128;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (solid[x, y]) continue;
                bool near = (x > 0 && solid[x - 1, y]) || (x < w - 1 && solid[x + 1, y]) ||
                            (y > 0 && solid[x, y - 1]) || (y < h - 1 && solid[x, y + 1]);
                if (near) b.SetPixel(x, y, c);
            }
    }

    // A speck on its own is noise at this size: anything with no neighbour goes.
    static void Despeckle(Bitmap b)
    {
        int w = b.Width, h = b.Height;
        List<Point> lone = new List<Point>();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (b.GetPixel(x, y).A == 0) continue;
                int n = 0;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i) {
                        if (i == 0 && j == 0) continue;
                        int u = x + i, v = y + j;
                        if (u >= 0 && v >= 0 && u < w && v < h && b.GetPixel(u, v).A > 0) ++n;
                    }
                if (n == 0) lone.Add(new Point(x, y));
            }
        foreach (Point p in lone) b.SetPixel(p.X, p.Y, Color.Transparent);
    }

    public static Bitmap Strip(Bitmap[] frames)
    {
        int w = frames[0].Width, h = frames[0].Height;
        Bitmap s = Blank(w * frames.Length, h);
        for (int f = 0; f < frames.Length; ++f)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) s.SetPixel(f * w + x, y, frames[f].GetPixel(x, y));
        return s;
    }

    // ------------------------------------------------------------------ comet
    // A round head at (hx, hy) and a tail streaming back from it along -x,
    // as one number a pixel: above one in the heart of the head, nothing
    // outside. The tail sways, frays toward its tip, and has noise scrolled
    // back through it a whole period over the frames.
    const int PERIOD = 8;
    const double CELL = 5.0;

    static double Comet(double x, double y, double hx, double hy, double R, double L, bool head,
                        double sway, double rough, double warmth, int frame, int frames, int seed)
    {
        double phase = frame / (double)frames;
        double scroll = phase * PERIOD * CELL;
        double dx = x - hx, dy = y - hy;
        double heat = 0;
        if (head) {
            double d = Math.Sqrt((dx - R * 0.10) * (dx - R * 0.10) + dy * dy);
            heat = 1.24 - d / R * 1.06;
        }
        if (dx < R * 0.2) {
            double t = Math.Max(0.0, -dx) / L;
            if (t <= 1.0) {
                double cy = hy + Math.Sin(t * 5.2 - phase * 2 * Math.PI) * sway * t;
                double n = Fbm((x + scroll) / CELL, y / 3.4, PERIOD, seed);
                // Tongues: a second, finer noise that opens gaps between them
                // further back, so the tail is flames and not a wedge.
                double lick = Fbm((x + scroll * 2) / (CELL * 0.5), y / 2.2, PERIOD * 2, seed + 3);
                double w = R * 0.94 * Math.Pow(1 - t, 0.62) * (0.80 + 0.40 * n) + 0.4;
                double lat = Math.Abs(y - cy) / w;
                if (lat < 1.5) {
                    double body = (warmth - t * 0.62) * (1 - Math.Pow(lat, 1.5));
                    body += (n - 0.5) * rough * (0.15 + t);
                    body -= Math.Max(0.0, 0.62 - lick) * t * 1.5;
                    heat = Math.Max(heat, body);
                }
            }
        }
        return heat;
    }

    // ------------------------------------------------------------------- fire
    public static Bitmap[] Fireball(int w, int h, double hx, double hy, double R, int frames, int seed)
    {
        double[] stops = { 0.84, 0.62, 0.42, 0.25, 0.13 };
        Color[] cols = {
            Color.FromArgb(255, 255, 250, 214), Color.FromArgb(255, 255, 222, 92),
            Color.FromArgb(255, 255, 154, 38),  Color.FromArgb(255, 230, 82, 24),
            Color.FromArgb(255, 158, 40, 22) };
        Bitmap[] outp = new Bitmap[frames];
        for (int f = 0; f < frames; ++f) {
            Bitmap b = Blank(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    double v = Comet(x + 0.5, y + 0.5, hx, hy, R, hx - 1.5, true, R * 0.5, 0.9, 0.86, f, frames, seed);
                    Color c = Ramp(v, stops, cols);
                    if (c.A > 0) b.SetPixel(x, y, c);
                }
            Despeckle(b);
            Outline(b, Color.FromArgb(215, 78, 18, 14));
            outp[f] = b;
        }
        return outp;
    }

    // A billow of flame with no ball at the head of it: what a Flamethrower
    // breathes. Blunt in front, ragged and licking away behind, and yellow at
    // its hottest -- the white heart is what makes a fireball a ball.
    public static Bitmap[] Flame(int w, int h, double cx, double cy, double rx, double ry, int frames, int seed)
    {
        double[] stops = { 0.80, 0.54, 0.31, 0.15 };
        Color[] cols = {
            Color.FromArgb(255, 255, 238, 150), Color.FromArgb(255, 255, 178, 52),
            Color.FromArgb(255, 236, 98, 28),   Color.FromArgb(255, 166, 44, 22) };
        Bitmap[] outp = new Bitmap[frames];
        for (int f = 0; f < frames; ++f) {
            double phase = f / (double)frames, scroll = phase * PERIOD * CELL;
            Bitmap b = Blank(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    double dx = (x + 0.5 - cx) / rx, dy = (y + 0.5 - cy) / ry;
                    double behind = dx < 0 ? -dx : 0.0;
                    double pinch = 1.0 + behind * 0.5;
                    double d = Math.Sqrt(dx * dx * (dx < 0 ? 0.55 : 1.0) + dy * dy * pinch * pinch);
                    double n = Fbm((x + scroll) / CELL, y / 3.0, PERIOD, seed);
                    double lick = Fbm((x + scroll * 2) / (CELL * 0.5), y / 2.0, PERIOD * 2, seed + 3);
                    double v = 1.08 - d + (n - 0.5) * 0.75 - Math.Max(0.0, 0.58 - lick) * behind * 1.4;
                    Color c = Ramp(v, stops, cols);
                    if (c.A > 0) b.SetPixel(x, y, c);
                }
            Despeckle(b);
            Outline(b, Color.FromArgb(215, 78, 18, 14));
            outp[f] = b;
        }
        return outp;
    }

    // A wave, seen from above and going toward +x: a bowed line of foam, the
    // glassy face of it behind that, and the churned water it leaves thinning out
    // to nothing. Seven of these go out abreast and have to read as one, so
    // nothing is outlined but the front, and both ends are let fade.
    public static Bitmap[] Wave(int w, int h, double front, double bulge, double depth, int frames, int seed)
    {
        Color foam = Color.FromArgb(255, 255, 255, 255), bright = Color.FromArgb(255, 208, 240, 255);
        Color light = Color.FromArgb(245, 140, 208, 252), mid = Color.FromArgb(235, 70, 148, 234);
        Color deep = Color.FromArgb(225, 44, 106, 200), rim = Color.FromArgb(210, 22, 56, 120);
        double cy = h / 2.0;
        Bitmap[] outp = new Bitmap[frames];
        for (int f = 0; f < frames; ++f) {
            double phase = f / (double)frames, scroll = phase * PERIOD * CELL;
            Bitmap b = Blank(w, h);
            for (int y = 0; y < h; ++y) {
                double yn = (y + 0.5 - cy) / cy;                                   // -1 .. 1 along the crest
                double xf = front - bulge * yn * yn + Math.Sin(yn * 5.0 + phase * 2 * Math.PI) * 0.9;
                double thick = depth * (1.0 - 0.55 * yn * yn);
                for (int x = 0; x < w; ++x) {
                    double u = xf - (x + 0.5);                                     // how far behind the front
                    if (u < -1.0 || u > thick) continue;
                    double n = Fbm((x + scroll) / CELL, y / 2.6, PERIOD, seed);
                    Color c;
                    if (u < 0.0)       c = Math.Abs(yn) < 0.86 ? rim : Color.Transparent;
                    else if (u < 1.8)  c = foam;
                    else if (u < 3.4)  c = n > 0.62 ? foam : bright;
                    else if (u < 6.2)  c = n > 0.70 ? bright : light;
                    else if (u < 9.5)  c = n > 0.74 ? light : mid;
                    else               c = deep;
                    // The back of it breaks up, and so do the two ends.
                    double fray = Math.Max((u - thick * 0.62) / (thick * 0.38), (Math.Abs(yn) - 0.72) / 0.28);
                    if (fray > 0.0 && n < 0.30 + fray * 0.55) continue;
                    if (c.A > 0) b.SetPixel(x, y, c);
                }
            }
            Despeckle(b);
            outp[f] = b;
        }
        return outp;
    }

    // ------------------------------------------------------------------ water
    public static Bitmap[] WaterWake(int w, int h, double hx, double hy, double R, int frames, int seed)
    {
        Color[] cols = {
            Color.FromArgb(255, 214, 242, 255), Color.FromArgb(240, 120, 190, 246),
            Color.FromArgb(235, 66, 138, 224),  Color.FromArgb(235, 38, 92, 178) };
        return Wake(w, h, hx, hy, R, frames, seed, cols, Color.FromArgb(200, 22, 56, 120));
    }

    // The same, of anything that runs: `cols` is four, brightest first.
    public static Bitmap[] Wake(int w, int h, double hx, double hy, double R, int frames, int seed,
                                Color[] cols, Color outline)
    {
        double[] stops = { 0.80, 0.52, 0.30, 0.16 };
        Bitmap[] outp = new Bitmap[frames];
        for (int f = 0; f < frames; ++f) {
            Bitmap b = Blank(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    double v = Comet(x + 0.5, y + 0.5, hx, hy, R * 0.8, hx - 1.5, false, R * 0.5, 1.1, 1.0, f, frames, seed);
                    Color c = Ramp(v, stops, cols);
                    if (c.A > 0) b.SetPixel(x, y, c);
                }
            Despeckle(b);
            Outline(b, outline);
            outp[f] = b;
        }
        return outp;
    }

    // A ball of water, seen as glass is: a dark rim, a highlight on the side the
    // light comes from and the light it has gathered on the side it leaves by.
    public static Bitmap[] WaterOrb(int size, double R, int frames, int seed)
    {
        Color[] water = {
            Color.FromArgb(255, 24, 62, 138),   Color.FromArgb(232, 44, 106, 200), Color.FromArgb(228, 70, 148, 234),
            Color.FromArgb(240, 140, 208, 252), Color.FromArgb(255, 208, 240, 255) };
        return Orb(size, R, frames, seed, water);
    }

    // The same, of anything that holds together in a ball: `pal` is rim, deep,
    // mid, light and bright.
    public static Bitmap[] Orb(int size, double R, int frames, int seed, Color[] pal)
    {
        Color rim = pal[0], deep = pal[1], mid = pal[2], light = pal[3], bright = pal[4];
        Color white  = Color.FromArgb(255, 255, 255, 255);
        double c0 = size / 2.0;
        Bitmap[] outp = new Bitmap[frames];
        for (int f = 0; f < frames; ++f) {
            double ph = f / (double)frames * 2 * Math.PI;
            Bitmap b = Blank(size, size);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    double dx = x + 0.5 - c0, dy = y + 0.5 - c0;
                    double th = Math.Atan2(dy, dx);
                    // It wobbles: two lobes going round one way, three the other.
                    double Rw = R * (1 + 0.075 * Math.Sin(2 * th + ph) + 0.04 * Math.Sin(3 * th - 2 * ph + 1.0));
                    double d = Math.Sqrt(dx * dx + dy * dy);
                    if (d > Rw) continue;
                    double r = d / Rw;
                    double u = dx / Rw, v = dy / Rw;
                    double toward = -(u * 0.64 + v * 0.77);        // +1 facing the light, top-left
                    Color c = mid;
                    if (d > Rw - 1.25) c = rim;
                    else {
                        if (toward > 0.30) c = deep;
                        if (toward < -0.25 && r > 0.50) c = light;
                        if (toward < -0.55 && r > 0.62 && r < 0.90) c = bright;
                        double hxp = u + 0.40, hyp = v + 0.44;
                        if (hxp * hxp + hyp * hyp * 1.5 < 0.060) c = white;
                        double sx = u + 0.12, sy = v + 0.70;
                        if (sx * sx + sy * sy < 0.012) c = bright;
                    }
                    b.SetPixel(x, y, c);
                }
            // Two small bubbles going round inside it.
            for (int k = 0; k < 2; ++k) {
                double a = ph * (k == 0 ? 1 : -1) + k * 2.4 + seed;
                int bx = (int)Math.Floor(c0 + Math.Cos(a) * R * 0.34 + (k == 0 ? 1.0 : -0.5));
                int by = (int)Math.Floor(c0 + Math.Sin(a) * R * 0.26 + R * 0.18);
                if (bx >= 0 && by >= 0 && bx < size && by < size && b.GetPixel(bx, by).A > 0 && b.GetPixel(bx, by) != rim)
                    b.SetPixel(bx, by, k == 0 ? bright : light);
            }
            outp[f] = b;
        }
        return outp;
    }

    // ------------------------------------------------------------------ earth
    static double Cross(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }
    static bool InTri(double px, double py, double ax, double ay, double bx, double by, double cx, double cy)
    {
        double d1 = Cross(bx - ax, by - ay, px - ax, py - ay);
        double d2 = Cross(cx - bx, cy - by, px - bx, py - by);
        double d3 = Cross(ax - cx, ay - cy, px - cx, py - cy);
        bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
        return !(neg && pos);
    }

    // A stone cut in facets about a point off its middle, turned a step a frame
    // and lit afresh each time from the top-left.
    public static Bitmap[] Rock(int size, double R, int frames, int seed)
    {
        Color[] stone = {
            Color.FromArgb(255, 226, 204, 160), Color.FromArgb(255, 186, 156, 108),
            Color.FromArgb(255, 140, 110, 74),  Color.FromArgb(255, 96, 72, 50) };
        return Shard(size, R, frames, seed, stone, Color.FromArgb(255, 74, 56, 40), Color.FromArgb(255, 46, 32, 24));
    }

    // The same cut in anything: `tones` is four, from the face that has the
    // light to the one that has none.
    public static Bitmap[] Shard(int size, double R, int frames, int seed, Color[] tones, Color fleck, Color outline)
    {
        const int K = 7;
        double[] lx = new double[K], ly = new double[K];
        for (int i = 0; i < K; ++i) {
            double ang = i * 2 * Math.PI / K + (Hash(i, 1, seed) - 0.5) * 0.55;
            double rad = R * (0.74 + 0.30 * Hash(i, 2, seed));
            if (i == 0) rad = R * 1.30;                 // the point of the shard
            if (i == 3) rad = R * 1.05;
            lx[i] = Math.Cos(ang) * rad * 1.10;
            ly[i] = Math.Sin(ang) * rad * 0.82;
        }
        double apx = R * 0.16, apy = -R * 0.12;
        double c0 = size / 2.0;
        Bitmap[] outp = new Bitmap[frames];
        for (int f = 0; f < frames; ++f) {
            double a = f * 2 * Math.PI / frames, ca = Math.Cos(a), sa = Math.Sin(a);
            double[] vx = new double[K], vy = new double[K];
            for (int i = 0; i < K; ++i) { vx[i] = lx[i] * ca - ly[i] * sa; vy[i] = lx[i] * sa + ly[i] * ca; }
            double ax = apx * ca - apy * sa, ay = apx * sa + apy * ca;
            Bitmap b = Blank(size, size);
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x) {
                    double px = x + 0.5 - c0, py = y + 0.5 - c0;
                    for (int i = 0; i < K; ++i) {
                        int j = (i + 1) % K;
                        if (!InTri(px, py, ax, ay, vx[i], vy[i], vx[j], vy[j])) continue;
                        double mx = (vx[i] + vx[j]) / 2 - ax, my = (vy[i] + vy[j]) / 2 - ay;
                        double len = Math.Max(0.001, Math.Sqrt(mx * mx + my * my));
                        double s = -(mx / len * 0.60 + my / len * 0.80);
                        int tone = s > 0.50 ? 0 : s > 0.0 ? 1 : s > -0.55 ? 2 : 3;
                        b.SetPixel(x, y, tones[tone]);
                        break;
                    }
                }
            // Flecks in the stone, which go round with it.
            for (int k = 0; k < 4; ++k) {
                double fx = (Hash(k, 5, seed) - 0.5) * R * 1.1, fy = (Hash(k, 6, seed) - 0.5) * R * 0.8;
                int qx = (int)Math.Floor(c0 + fx * ca - fy * sa), qy = (int)Math.Floor(c0 + fx * sa + fy * ca);
                if (qx > 0 && qy > 0 && qx < size - 1 && qy < size - 1 && b.GetPixel(qx, qy).A > 0 &&
                    b.GetPixel(qx + 1, qy).A > 0 && b.GetPixel(qx - 1, qy).A > 0 &&
                    b.GetPixel(qx, qy + 1).A > 0 && b.GetPixel(qx, qy - 1).A > 0)
                    b.SetPixel(qx, qy, fleck);
            }
            Outline(b, outline);
            outp[f] = b;
        }
        return outp;
    }

    // -------------------------------------------------------------------- air
    struct Sample { public double x, y, s, th; public int line; }

    // One line of moving air: along from (x0, y) to (x1, y) and then over in a
    // curl, upward (-1) or downward (+1). `s` runs 0 at the tail to 1 in the
    // middle of the curl; it is fattest between and comes to nothing at both.
    static void Stream(List<Sample> into, int line, double x0, double x1, double y, int curl, double rc,
                       double thick, double wag, double phase)
    {
        double lineLen = x1 - x0, sweep = 1.3 * 2 * Math.PI;
        double curlLen = rc * sweep * 0.62;
        double total = lineLen + curlLen;
        for (double d = 0; d <= lineLen; d += 0.4) {
            double k = d / lineLen;
            Sample p = new Sample();
            p.x = x0 + d;
            p.y = y + Math.Sin(k * 4.4 + phase * 2 * Math.PI + line * 1.7) * wag * (1 - k) * (1 - k);
            p.s = d / total; p.line = line;
            into.Add(p);
        }
        double cy = y + curl * rc;
        for (double u = 0; u <= 1.0; u += 0.01) {
            double ang = (curl < 0 ? Math.PI / 2 - u * sweep : -Math.PI / 2 + u * sweep);
            double r = rc * (1 - 0.66 * u);
            Sample p = new Sample();
            p.x = x1 + Math.Cos(ang) * r;
            p.y = cy + Math.Sin(ang) * r;
            p.s = (lineLen + u * curlLen) / total; p.line = line;
            into.Add(p);
        }
        for (int i = 0; i < into.Count; ++i) {
            Sample p = into[i];
            if (p.line != line) continue;
            // A long thin tail, and a curl that thins as it winds in, so that the
            // eye of it stays open: wound at full width it closes into a blob.
            double tail = Math.Pow(Math.Min(1.0, p.s / 0.42), 0.7);
            double tip  = Math.Pow(Math.Min(1.0, (1 - p.s) * 2.3), 0.9);
            p.th = thick * Math.Min(tail, tip);
            into[i] = p;
        }
    }

    // `layout` is rows of { x0, x1, y, curl, rc, thick }, in pixels. `blade` is
    // { cx, cy, radius, depth } for the edge a greater gust has, or null.
    public static Bitmap[] Gust(int w, int h, double[][] layout, double[] blade, int frames)
    {
        Color core  = Color.FromArgb(255, 250, 254, 255);
        Color pale  = Color.FromArgb(240, 204, 232, 246);
        Color faint = Color.FromArgb(205, 158, 200, 226);
        Color edge  = Color.FromArgb(175, 84, 128, 164);
        Bitmap[] outp = new Bitmap[frames];
        for (int f = 0; f < frames; ++f) {
            double phase = f / (double)frames;
            List<Sample> pts = new List<Sample>();
            for (int i = 0; i < layout.Length; ++i)
                Stream(pts, i, layout[i][0], layout[i][1], layout[i][2], (int)layout[i][3], layout[i][4],
                       layout[i][5], 1.6, phase);
            Bitmap b = Blank(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    double px = x + 0.5, py = y + 0.5;
                    double best = 1e9; Sample hit = new Sample();
                    foreach (Sample p in pts) {
                        if (p.th < 0.7) continue;
                        double d = Math.Sqrt((p.x - px) * (p.x - px) + (p.y - py) * (p.y - py)) - p.th / 2;
                        if (d < best) { best = d; hit = p; }
                    }
                    Color c = Color.Transparent;
                    if (best <= 0.0) {
                        // Brightness runs along each line toward its curl.
                        double flow = 0.5 + 0.5 * Math.Sin(2 * Math.PI * (hit.s * 2.4 - phase) + hit.line * 2.1);
                        c = flow > 0.55 ? core : flow > 0.2 ? pale : faint;
                    }
                    if (blade != null) {
                        double bx = px - blade[0], by = py - blade[1];
                        double outer = Math.Sqrt(bx * bx + by * by);
                        double inner = Math.Sqrt((bx + blade[3]) * (bx + blade[3]) + by * by);
                        if (outer <= blade[2] && inner >= blade[2] * 1.02 && bx > 0) {
                            double depth = blade[2] - outer;
                            double shimmer = Math.Sin(Math.Atan2(by, bx) * 5 + phase * 2 * Math.PI);
                            c = depth < 1.6 ? core : (shimmer > 0.1 ? pale : faint);
                        }
                    }
                    if (c.A > 0) b.SetPixel(x, y, c);
                }
            Despeckle(b);
            Outline(b, edge);
            outp[f] = b;
        }
        return outp;
    }

    // An edge of air and nothing else: the greater gust's crescent, on its own.
    public static Bitmap[] Slash(int w, int h, double[] blade, int frames)
    {
        return Gust(w, h, new double[0][], blade, frames);
    }

    // A knife, end over end: a bright blade and a dark grip, a step of the turn a frame.
    public static Bitmap[] Knife(int size, int frames)
    {
        Color blade = Color.FromArgb(255, 232, 238, 248), spine = Color.FromArgb(255, 150, 160, 184);
        Color grip = Color.FromArgb(255, 96, 66, 44), rim = Color.FromArgb(230, 34, 30, 40);
        double c0 = size / 2.0;
        Bitmap[] outp = new Bitmap[frames];
        for (int f = 0; f < frames; ++f) {
            double a = f * 2 * Math.PI / frames, ca = Math.Cos(a), sa = Math.Sin(a);
            Bitmap b = Blank(size, size);
            for (double d = -4.5; d <= 4.5; d += 0.25) {
                int x = (int)Math.Floor(c0 + ca * d), y = (int)Math.Floor(c0 + sa * d);
                if (x < 0 || y < 0 || x >= size || y >= size) continue;
                b.SetPixel(x, y, d < -1.5 ? grip : d > 3.0 ? blade : (d > 0.5 ? blade : spine));
            }
            Outline(b, rim);
            outp[f] = b;
        }
        return outp;
    }

    // ------------------------------------------------------------------- glow
    public static Bitmap Glow(int size)
    {
        Bitmap b = Blank(size, size);
        double c0 = size / 2.0;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                double d = Math.Sqrt((x + 0.5 - c0) * (x + 0.5 - c0) + (y + 0.5 - c0) * (y + 0.5 - c0)) / c0;
                if (d >= 1) continue;
                double a = (1 - d) * (1 - d);
                b.SetPixel(x, y, Color.FromArgb((int)(a * 255), 255, 255, 255));
            }
        return b;
    }
}
'@
Add-Type -TypeDefinition $source -ReferencedAssemblies System.Drawing

$report = @()
function Save-Strip($name, $frames, $pivotX, $pivotY) {
    $strip = [Fx]::Strip($frames)
    $path = Join-Path $out "$name.png"
    $strip.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $script:report += ("  {0,-20} {1,3} x {2,-3} {3} frames   pivot {4}, {5}" -f $name, $frames[0].Width, $frames[0].Height, $frames.Length, $pivotX, $pivotY)
    $strip.Dispose()
    foreach ($f in $frames) { $f.Dispose() }
}

$N = 8

# --- fire: the head is held, the tail streams back from it
Save-Strip "fireball"         ([Fx]::Fireball(38, 18, 30.0, 9.0, 6.4, $N, 11))  30 9
Save-Strip "fireball_greater" ([Fx]::Fireball(54, 26, 43.0, 13.0, 9.2, $N, 23)) 43 13

# --- water: an upright orb, and a wake under it that is turned to follow
Save-Strip "water_orb"          ([Fx]::WaterOrb(18, 7.4, $N, 3))  9 9
Save-Strip "water_orb_greater"  ([Fx]::WaterOrb(26, 10.6, $N, 5)) 13 13
Save-Strip "water_wake"         ([Fx]::WaterWake(30, 16, 24.0, 8.0, 7.4, $N, 31))   24 8
Save-Strip "water_wake_greater" ([Fx]::WaterWake(42, 22, 34.0, 11.0, 10.6, $N, 37)) 34 11

# --- earth: a shard, tumbling
Save-Strip "rock_shard"         ([Fx]::Rock(22, 7.2, $N, 41))  11 11
Save-Strip "rock_shard_greater" ([Fx]::Rock(30, 10.2, $N, 43)) 15 15

# --- air: x0, x1, y, curl (-1 up, +1 down), curl radius, thickness. No bigger
#     than this: what it strikes is a circle six or eight pixels round the
#     pivot, and a gust three times that tall sails through things untouched.
$gust = @(
    ,[double[]]@(5, 20, 7.5, -1, 3.2, 2.2)
    ,[double[]]@(1, 28, 13.5, -1, 4.4, 2.8)
    ,[double[]]@(7, 21, 18.5, 1, 3.0, 2.2)
)
Save-Strip "gust" ([Fx]::Gust(38, 26, [double[][]]$gust, $null, $N)) 27 13

$gale = @(
    ,[double[]]@(7, 23, 7.5, -1, 3.6, 2.4)
    ,[double[]]@(1, 30, 15.5, -1, 5.0, 3.2)
    ,[double[]]@(4, 25, 23.5, 1, 3.6, 2.6)
)
Save-Strip "gust_greater" ([Fx]::Gust(52, 34, [double[][]]$gale, [double[]]@(32.0, 17.0, 16.0, 4.6), $N)) 38 17

# --- the ancient spells that are not violet: the same shapes in other stuff
function Argb($a, $r, $g, $b) { return [System.Drawing.Color]::FromArgb($a, $r, $g, $b) }
$acid  = [System.Drawing.Color[]]@((Argb 255 34 84 22), (Argb 232 66 140 40), (Argb 228 116 196 60), (Argb 240 178 232 110), (Argb 255 226 250 170))
$acidW = [System.Drawing.Color[]]@((Argb 255 226 250 170), (Argb 240 160 222 90), (Argb 235 100 176 52), (Argb 235 54 116 30))
Save-Strip "acid_glob" ([Fx]::Orb(14, 5.6, $N, 7, $acid)) 7 7
Save-Strip "acid_wake" ([Fx]::Wake(24, 12, 18.0, 6.0, 5.6, $N, 53, $acidW, (Argb 200 26 64 18))) 18 6

$blood  = [System.Drawing.Color[]]@((Argb 255 84 12 22), (Argb 236 140 22 36), (Argb 232 196 40 52), (Argb 240 236 96 96), (Argb 255 255 190 180))
$bloodW = [System.Drawing.Color[]]@((Argb 255 255 190 180), (Argb 240 226 80 84), (Argb 235 170 30 44), (Argb 235 104 14 28))
Save-Strip "blood_orb"  ([Fx]::Orb(16, 6.4, $N, 9, $blood)) 8 8
Save-Strip "blood_wake" ([Fx]::Wake(24, 12, 18.0, 6.0, 6.4, $N, 59, $bloodW, (Argb 200 64 8 18))) 18 6

$ice = [System.Drawing.Color[]]@((Argb 255 244 252 255), (Argb 255 190 230 252), (Argb 250 128 186 236), (Argb 250 78 130 204))
Save-Strip "frost_shard" ([Fx]::Shard(20, 6.6, $N, 47, $ice, (Argb 255 255 255 255), (Argb 255 40 72 130))) 10 10

# --- the elements' own staves: what their second, third and fourth spells throw.
#     Each at the size it is drawn: a strip scaled up in the engine is a strip of
#     fat pixels beside everything else on the screen.
Save-Strip "flame_billow"      ([Fx]::Flame(30, 18, 19.0, 9.0, 9.5, 6.4, $N, 67)) 19 9
Save-Strip "water_orb_cannon"  ([Fx]::WaterOrb(34, 14.0, $N, 13)) 17 17
Save-Strip "water_wake_cannon" ([Fx]::WaterWake(56, 28, 45.0, 14.0, 14.0, $N, 61)) 45 14
Save-Strip "wave_crest"        ([Fx]::Wave(24, 30, 19.0, 5.0, 17.0, $N, 71)) 17 15
Save-Strip "rock_shard_small"  ([Fx]::Rock(14, 4.6, $N, 45)) 7 7

# --- what the armoury throws, and the air's third spell
Save-Strip "throwing_knife" ([Fx]::Knife(12, $N)) 6 6
Save-Strip "air_slash" ([Fx]::Slash(24, 40, [double[]]@(4.0, 20.0, 18.0, 5.0), $N)) 16 20

$glow = [Fx]::Glow(32)
$glow.Save((Join-Path $out "glow.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$glow.Dispose()

Write-Host "Effects written to assets\effects:"
$report | ForEach-Object { Write-Host $_ }
Write-Host "  glow                  32 x 32"
