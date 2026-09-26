#version 450
// Shapes of light drawn over a plain quad, each worked out per pixel:
//
//   0 the mana shield   a dome with its skin brightest where it is seen edge
//                       on, a honeycomb in it, and a ripple spreading from
//                       where it was last struck; its foot a ring on the ground
//   1 an electro-node   an orb with the charge swirling round inside it
//   2 stained glass     the light of a high window lying on the floor: a lancet,
//                       leaded into panes of crimson, gold, violet and blue
//   3 a halo            the lit air round a lamp or a brazier after dark
//   4 a slash           a crescent swept round, bright at its head and edge
//   5 an impact         a flash, rays and a spreading ring where a blow lands
//   6 a thrust          a line driven out, widest at its point
//   7 a cross cut       two strokes crossing
//   8 a casting circle  two rings on the ground, ticks turning between them
//   9 a vortex          spiral arms wheeling on the ground, flowing in or out
//  10 cracks            fissures running out from where the ground was struck
//  11 a pillar          a column of light standing up off a ring on the ground
//  12 a sigil           a rune circle turning: glyphs between two rings, a star
//  13 a streak          speed lines left behind along a line of travel
//  14 a reticle         brackets closing in on what has been marked
//  15 shards            splinters flung out from a point, tumbling as they go
//  16 a wave            a front of force running out across the ground
//
// Shapes 4 to 16 are the strikes' (World::DrawStrikes) and read their four
// numbers from `hit`; 9 to 16 are the techniques' and the abilities'.
//
// Worked in world pixels (`size` says how many screen pixels one is), so the
// shapes are as square-edged as everything else.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 3, binding = 0) uniform Fx {
    vec4 kind;     // kind; fade 0..1; seconds; seed
    vec4 colour;   // rgb; strength
    vec4 size;     // the quad's width and height in screen px; zoom; the dome's foot, as a share of its height
    vec4 hit;      // the dome: where it was struck (x, y on the dome, -1..1); how long ago, 0..1 (<0 none)
} fx;

float Hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

void main() {
    int kind = int(fx.kind.x + 0.5);
    float fade = fx.kind.y;
    float t = fx.kind.z;
    vec2 cells = max(floor(fx.size.xy / fx.size.z), vec2(1.0));
    vec2 q = (floor(v_uv * cells) + 0.5) / cells;          // snapped to world pixels
    vec3 c = fx.colour.rgb;
    float a = 0.0;
    vec3 rgb = c;

    if (kind == 0) {
        // The quad runs from the crown (y = -1) past the foot (0) to the edge
        // of the ring on the ground (g).
        float g = fx.size.w;
        vec2 p = vec2(q.x * 2.0 - 1.0, mix(-1.0, g, q.y));
        if (p.y <= 0.0) {
            float r = length(p);
            if (r < 1.0) {
                float rim = smoothstep(0.72, 1.0, r);
                // A honeycomb, faint, fixed on the dome: a cell every few
                // pixels, its walls a pixel thick.
                vec2 h = p * 5.0;
                h.x *= 1.1547;
                h.y += mod(floor(h.x), 2.0) * 0.5;
                vec2 f = fract(h);
                float comb = (f.x < 0.2 || f.y < 0.2) ? 1.0 : 0.0;
                a = 0.16 + 0.5 * rim + 0.14 * comb;
                rgb = mix(c, vec3(0.93, 0.88, 1.0), rim * 0.4 + comb * 0.2);
                if (fx.hit.z >= 0.0) {
                    float ring = length(p - fx.hit.xy) - fx.hit.z * 1.6;
                    float wave = exp(-ring * ring / 0.02) * (1.0 - fx.hit.z);
                    a += wave * 0.9;
                    rgb = mix(rgb, vec3(1.0, 0.97, 1.0), wave);
                }
                // A slow shimmer running up it.
                a += 0.06 * step(0.94, fract(p.y * 3.0 - t * 0.8 + p.x * 0.5));
            }
        } else {
            vec2 e = vec2(p.x, p.y / max(g, 0.01));
            float r = length(e);
            if (r < 1.0) {
                a = 0.12 * (1.0 - r * r) + (r > 0.86 ? 0.45 : 0.0);
                rgb = mix(c, vec3(0.93, 0.88, 1.0), r > 0.86 ? 0.5 : 0.0);
            }
        }
        a *= fade;
    } else if (kind == 1) {
        vec2 p = q * 2.0 - 1.0;
        float r = length(p);
        if (r < 1.0) {
            float ang = atan(p.y, p.x);
            float swirl = sin(ang * 3.0 + t * 7.0 - r * 9.0);
            float streak = step(0.82, swirl) * smoothstep(0.95, 0.2, r);
            a = 0.12 + 0.18 * (1.0 - r) + streak * 0.8 + (r > 0.84 ? 0.6 : 0.0);
            rgb = mix(c, vec3(1.0, 1.0, 0.92), streak + (r > 0.84 ? 0.4 : 0.0));
            if (Hash(floor(p * 9.0) + floor(t * 12.0)) > 0.97) { a = 1.0; rgb = vec3(1.0); }
        }
        a *= fade;
    } else if (kind == 2) {
        // A lancet: straight sides, a pointed head. The light falls at a slant,
        // so the pool is sheared the way the sun comes in.
        vec2 p = q * 2.0 - 1.0;
        p.x -= p.y * 0.25;
        bool inside = abs(p.x) < 0.8 && p.y > -0.55;
        if (!inside && p.y <= -0.55 && p.y > -1.0) inside = abs(p.x) < 0.8 * (p.y + 1.0) / 0.45;
        if (inside) {
            vec2 pane = floor(vec2((p.x + 0.8) / 0.4, (p.y + 1.0) / 0.45));
            vec2 in_pane = vec2(fract((p.x + 0.8) / 0.4), fract((p.y + 1.0) / 0.45));
            bool lead = in_pane.x < 0.08 || in_pane.y < 0.07;
            float h = Hash(pane + fx.kind.w);
            vec3 glass = h < 0.3 ? vec3(0.85, 0.12, 0.16)
                       : h < 0.55 ? vec3(1.0, 0.72, 0.22)
                       : h < 0.8 ? vec3(0.55, 0.28, 0.9)
                                 : vec3(0.25, 0.4, 0.95);
            float soft = smoothstep(1.0, 0.7, abs(p.x) / 0.8) * smoothstep(1.0, 0.6, p.y);
            rgb = glass;
            a = lead ? 0.0 : 0.55 * soft;
        }
        a *= fade;
    } else if (kind == 4) {
        // A slash: a crescent swept from angle hit.x through hit.y radians,
        // drawn as far as hit.z of the way round -- brightest at its head and
        // along its outer edge, where the blade is -- with sparks thrown off
        // near the head. hit.w is how thick it is, as a share of its radius.
        vec2 p = q * 2.0 - 1.0;
        float r = length(p);
        float sweep = fx.hit.y, head = fx.hit.z, thick = max(fx.hit.w, 0.02);
        float inner = 1.0 - thick;
        if (r > inner && r < 1.0) {
            float d = atan(p.y, p.x) - fx.hit.x;
            float s = (sweep >= 0.0 ? mod(d, 6.2831853) : mod(-d, 6.2831853)) / max(abs(sweep), 0.001);
            if (s <= head) {
                float behind = head - s;
                float tail = clamp(1.0 - behind / 0.55, 0.0, 1.0);
                float edge = (r - inner) / thick;
                float core = smoothstep(0.45, 0.95, edge);
                a = tail * (0.35 + 0.65 * core);
                rgb = mix(c, vec3(1.0), core * tail * 0.75);
                if (behind < 0.25 && Hash(floor(p * 18.0) + fx.kind.w + floor(t * 20.0)) > 0.93) { a = 1.0; rgb = vec3(1.0); }
            }
        }
        a *= fade;
    } else if (kind == 5) {
        // A blow landing: a flash at its heart, rays thrown out that shorten as
        // it goes, and a ring spreading. hit.x runs 0..1 over its life, hit.y
        // is how many rays (none under three), hit.z flattens it onto the
        // ground (1 round), hit.w is the ring's width (0 none).
        float prog = fx.hit.x, spikes = fx.hit.y, squash = max(fx.hit.z, 0.2), ring_w = fx.hit.w;
        vec2 p = q * 2.0 - 1.0;
        p.y /= squash;
        float r = length(p);
        float ray = 0.0;
        if (spikes >= 2.5) {
            float ang = atan(p.y, p.x);
            float k = floor((ang + 3.14159265) / 6.2831853 * spikes);
            float mid = (k + 0.5) / spikes * 6.2831853 - 3.14159265;
            float off = abs(ang - mid) * r;
            float len = (0.55 + 0.45 * Hash(vec2(k, fx.kind.w))) * (1.0 - prog * 0.6);
            if (r < len && off < 0.07 * (1.0 - r / len) + 0.02) ray = (1.0 - r / len) * (1.0 - prog * 0.8);
        }
        float heart = exp(-r * r / 0.012) * (1.0 - prog) * 0.75;
        float ring = 0.0;
        if (ring_w > 0.0) {
            float rr = abs(r - (0.25 + 0.75 * prog));
            if (rr < ring_w) ring = (1.0 - rr / ring_w) * (1.0 - prog);
        }
        a = max(max(ray, heart), ring);
        rgb = mix(c, vec3(1.0), max(heart, ray * 0.5));
        a *= fade;
    } else if (kind == 6) {
        // A thrust: a line driven out from hit.w along angle hit.x, as far as
        // hit.y of the way to the edge, widest and brightest at its point.
        float ang = fx.hit.x, head = fx.hit.y, width = max(fx.hit.z, 0.01), from = fx.hit.w;
        vec2 p = q * 2.0 - 1.0;
        vec2 dir = vec2(cos(ang), sin(ang));
        float along = dot(p, dir);
        float across = abs(dot(p, vec2(-dir.y, dir.x)));
        if (along > from && along < head) {
            float taper = (along - from) / max(head - from, 0.001);
            float w = width * (0.3 + 0.7 * taper);
            if (across < w) {
                float core = 1.0 - across / w;
                a = taper * (0.4 + 0.6 * core);
                rgb = mix(c, vec3(1.0), core * taper * 0.8);
            }
        }
        a *= fade;
    } else if (kind == 7) {
        // A cross cut: two strokes crossing on angle hit.x, the second coming
        // a moment after the first, both grown by hit.y; hit.z their width.
        float ang = fx.hit.x, prog = fx.hit.y, width = max(fx.hit.z, 0.01);
        vec2 p = q * 2.0 - 1.0;
        for (int i = 0; i < 2; ++i) {
            float grow = clamp(prog * 2.0 - float(i) * 0.5, 0.0, 1.0);
            float a2 = ang + (i == 0 ? 0.785398 : -0.785398);
            vec2 dir = vec2(cos(a2), sin(a2));
            float along = dot(p, dir);
            float across = abs(dot(p, vec2(-dir.y, dir.x)));
            if (abs(along) < grow * 0.9 && across < width) {
                float core = 1.0 - across / width;
                float v = (0.5 + 0.5 * core) * (0.4 + 0.6 * (1.0 - abs(along) / 0.9));
                if (v > a) { a = v; rgb = mix(c, vec3(1.0), core * 0.8); }
            }
        }
        a *= fade;
    } else if (kind == 8) {
        // A circle cast on the ground: two rings, ticks turning between them.
        // hit.x opens it and lets it go; hit.z flattens it onto the ground.
        float prog = fx.hit.x, squash = max(fx.hit.z, 0.2);
        vec2 p = q * 2.0 - 1.0;
        p.y /= squash;
        float r = length(p);
        float open = smoothstep(0.0, 0.3, prog);
        if (abs(r - 0.92 * open) < 0.05) a = 0.9;
        else if (abs(r - 0.62 * open) < 0.035) a = 0.7;
        else if (r > 0.62 * open && r < 0.92 * open) {
            float tick = fract((atan(p.y, p.x) + t * 1.8) / 6.2831853 * 16.0);
            if (tick < 0.18) a = 0.55;
        }
        rgb = mix(c, vec3(1.0), a > 0.8 ? 0.5 : 0.1);
        a *= fade * (1.0 - smoothstep(0.7, 1.0, prog));
    } else if (kind == 9) {
        // A vortex on the ground: arms spiralling round the middle, wheeling
        // the way hit.y's sign says (as many arms as its size), spreading out
        // from the middle as hit.x runs 0..1 -- or, with hit.w below nought,
        // drawn in toward it. hit.z flattens it onto the ground.
        float prog = fx.hit.x, arms = max(abs(fx.hit.y), 1.0), turn = fx.hit.y < 0.0 ? -1.0 : 1.0;
        float squash = max(fx.hit.z, 0.2), flow = fx.hit.w;
        vec2 p = q * 2.0 - 1.0;
        p.y /= squash;
        float r = length(p);
        float reach = flow >= 0.0 ? 0.25 + 0.75 * smoothstep(0.0, 0.5, prog) : 1.0 - 0.7 * prog;
        if (r < reach) {
            float ang = atan(p.y, p.x);
            float s = fract((ang * arms - turn * r * 7.0 + turn * t * 9.0 + flow * r * 3.0) / 6.2831853);
            float arm = 1.0 - smoothstep(0.0, 0.2, abs(s - 0.5));
            float edge = 1.0 - smoothstep(reach - 0.25, reach, r);
            float hub = smoothstep(0.02, 0.2, r);
            a = (arm * (0.3 + 0.6 * r) + 0.1) * edge * hub;
            rgb = mix(c, vec3(1.0), arm * r * 0.55);
            if (arm > 0.6 && Hash(floor(p * 14.0) + fx.kind.w + floor(t * 16.0)) > 0.92) { a = 1.0; rgb = vec3(1.0); }
        }
        a *= fade;
    } else if (kind == 10) {
        // Cracks: hit.y fissures running out from the middle, jagged, each
        // its own length, grown as hit.x runs, glowing white-hot at first
        // (hit.w, 0..1) and cooling to the colour. hit.z flattens them.
        float prog = fx.hit.x, n = max(fx.hit.y, 3.0), squash = max(fx.hit.z, 0.2), heat = fx.hit.w;
        vec2 p = q * 2.0 - 1.0;
        p.y /= squash;
        float r = length(p);
        float grow = smoothstep(0.0, 0.3, prog);
        float ang = atan(p.y, p.x) + 3.14159265;
        float k = floor(ang / 6.2831853 * n + 0.5);
        float best = 1e3, len = 0.0;
        for (int j = -1; j <= 1; ++j) {
            float kk = mod(k + float(j), n);
            float seg = floor(r / 0.13);
            float f = fract(r / 0.13);
            float o0 = Hash(vec2(kk, seg) + fx.kind.w) - 0.5, o1 = Hash(vec2(kk, seg + 1.0) + fx.kind.w) - 0.5;
            float at = (kk + 0.35 * (Hash(vec2(kk, 91.0) + fx.kind.w) - 0.5)) / n * 6.2831853 + mix(o0, o1, f) * 0.5 / (1.0 + 3.0 * r);
            float d = abs(mod(ang - at + 3.14159265, 6.2831853) - 3.14159265) * r;
            if (d < best) { best = d; len = (0.5 + 0.5 * Hash(vec2(kk, 7.0) + fx.kind.w)) * grow; }
        }
        float w = 0.045 * (1.0 - 0.6 * r / max(len, 0.01));
        if (r < len && best < w) {
            float core = 1.0 - best / w;
            a = (0.45 + 0.55 * core) * (1.0 - 0.5 * r / len);
            rgb = mix(c, vec3(1.0, 0.94, 0.8), core * heat * (1.0 - prog));
        }
        // The heart of it: a bright pit where the blow went in.
        float pit = exp(-r * r / 0.01) * (1.0 - prog);
        if (pit > a) { a = pit; rgb = mix(c, vec3(1.0), 0.7); }
        a *= fade;
    } else if (kind == 11) {
        // A pillar: a column of light hit.y wide (a share of the quad) shooting
        // up from a ring on the ground at the foot of the quad, thinning away
        // as hit.x runs out, with motes rising up it (hit.w, 0..1). The foot
        // is at 0.7 of the way down; hit.z flattens its ring.
        float prog = fx.hit.x, w = max(fx.hit.y, 0.03), squash = max(fx.hit.z, 0.1), motes = fx.hit.w;
        vec2 p = q * 2.0 - 1.0;
        const float foot = 0.7;
        float rise = smoothstep(0.0, 0.2, prog);
        float top = foot - (foot + 1.0) * rise;
        float cw = w * (0.45 + 0.55 * (1.0 - smoothstep(0.45, 1.0, prog)));
        if (p.y < foot && p.y > top) {
            float across = abs(p.x) / cw;
            if (across < 1.0) {
                float core = 1.0 - across;
                float up = smoothstep(top, top + 0.35, p.y) * (0.6 + 0.4 * smoothstep(foot - 0.6, foot, p.y));
                a = (0.25 + 0.75 * core * core) * up;
                rgb = mix(c, vec3(1.0), core * core * 0.8);
            }
        }
        vec2 e = vec2(p.x / (w * 2.6), (p.y - foot) / (w * 2.6 * squash));
        float er = length(e);
        float ring_r = 0.5 + 0.5 * prog;
        if (abs(er - ring_r) < 0.14) { float v = (1.0 - abs(er - ring_r) / 0.14) * (1.0 - prog); if (v > a) { a = v; rgb = mix(c, vec3(1.0), 0.4); } }
        if (motes > 0.0 && p.y < foot && p.y > top && abs(p.x) < cw * 2.2) {
            vec2 cell = floor(vec2(p.x * 12.0, p.y * 12.0 + t * 30.0));
            if (Hash(cell + fx.kind.w) > 1.0 - 0.12 * motes) { a = max(a, 0.9); rgb = mix(c, vec3(1.0), 0.6); }
        }
        a *= fade;
    } else if (kind == 12) {
        // A sigil: two rings on the ground with runes lettered round between
        // them and a star of hit.y points inside (none under three), opening
        // as hit.x starts and turning at hit.w. hit.z flattens it.
        float prog = fx.hit.x, pts = fx.hit.y, squash = max(fx.hit.z, 0.2), spin = fx.hit.w;
        vec2 p = q * 2.0 - 1.0;
        p.y /= squash;
        float open = smoothstep(0.0, 0.25, prog);
        p /= max(open, 0.05);
        float r = length(p);
        float ang = atan(p.y, p.x) + t * spin;
        if (abs(r - 0.94) < 0.045) a = 0.85;
        else if (abs(r - 0.66) < 0.035) a = 0.7;
        else if (r > 0.7 && r < 0.9) {
            // A rune a cell: three by two little blocks, lit by the dice.
            float cells = 22.0;
            float u = fract((ang / 6.2831853 + 1.0) * cells);
            float v = (r - 0.7) / 0.2;
            vec2 bit = floor(vec2(u * 3.0, v * 2.0));
            float glyph = Hash(vec2(floor((ang / 6.2831853 + 1.0) * cells), 3.0) + fx.kind.w);
            if (u > 0.15 && u < 0.85 && v > 0.15 && v < 0.85 && Hash(bit + glyph * 17.0) > 0.45) a = 0.75;
        } else if (pts >= 3.0 && r < 0.66) {
            // The star: every other point joined, so five make a pentagram.
            float step = pts >= 5.0 ? 2.0 : 1.0;
            for (int i = 0; i < 8; ++i) {
                if (float(i) >= pts) break;
                float a0 = float(i) / pts * 6.2831853 - t * spin, a1 = (float(i) + step) / pts * 6.2831853 - t * spin;
                vec2 A = vec2(cos(a0), sin(a0)) * 0.64, B = vec2(cos(a1), sin(a1)) * 0.64;
                vec2 ab = B - A;
                float h = clamp(dot(p - A, ab) / dot(ab, ab), 0.0, 1.0);
                if (length(p - A - ab * h) < 0.035) a = max(a, 0.8);
            }
        }
        rgb = mix(c, vec3(1.0), a > 0.8 ? 0.45 : 0.15);
        a *= fade * (1.0 - smoothstep(0.75, 1.0, prog));
    } else if (kind == 13) {
        // A streak: speed lines left behind along angle hit.x, a bright one
        // down the middle and shorter ones either side, sliding back and going
        // out as hit.y runs. hit.z is their width, hit.w how long they are,
        // as shares of the quad.
        float ang = fx.hit.x, prog = fx.hit.y, width = max(fx.hit.z, 0.01), len = max(fx.hit.w, 0.1);
        vec2 p = q * 2.0 - 1.0;
        vec2 dir = vec2(cos(ang), sin(ang));
        float along = dot(p, dir);
        float across = dot(p, vec2(-dir.y, dir.x));
        float lane = floor(across / (width * 2.2) + 0.5);
        if (abs(lane) <= 3.0) {
            float h = Hash(vec2(lane, 5.0) + fx.kind.w);
            float l = len * (lane == 0.0 ? 1.0 : 0.45 + 0.4 * h) * (1.0 - abs(lane) * 0.15);
            float head = len * 0.5 - (lane == 0.0 ? 0.0 : 0.25 * h) - prog * 0.5;
            float tail = head - l;
            float off = abs(across - lane * width * 2.2);
            float lw = width * (lane == 0.0 ? 1.0 : 0.55);
            if (along < head && along > tail && off < lw) {
                float k = (along - tail) / max(l, 0.01);
                a = k * (0.5 + 0.5 * (1.0 - off / lw)) * (1.0 - prog * 0.6);
                rgb = mix(c, vec3(1.0), k * (lane == 0.0 ? 0.8 : 0.3));
            }
        }
        a *= fade;
    } else if (kind == 14) {
        // A reticle: hit.y brackets closing in on the middle as hit.x runs,
        // a ring of dashes turning round them at hit.w and a dot at the heart.
        // hit.z flattens it (1 stands up, for over a head).
        float prog = fx.hit.x, n = max(fx.hit.y, 2.0), squash = max(fx.hit.z, 0.2), spin = fx.hit.w;
        vec2 p = q * 2.0 - 1.0;
        p.y /= squash;
        float r = length(p);
        float ang = atan(p.y, p.x);
        float close = 1.0 - 0.45 * smoothstep(0.0, 0.4, prog);
        float sector = fract((ang - t * spin * 0.5) / 6.2831853 * n + 0.5);
        if (abs(r - close * 0.8) < 0.06 && abs(sector - 0.5) < 0.16) a = 0.95;
        else if (abs(sector - 0.5) < 0.035 && r > close * 0.52 && r < close * 0.8) a = 0.9;
        float dash = fract((ang + t * spin) / 6.2831853 * 24.0);
        if (abs(r - 0.96) < 0.03 && dash < 0.5) a = max(a, 0.5);
        if (r < 0.07 * (0.8 + 0.4 * sin(t * 12.0))) a = 1.0;
        rgb = mix(c, vec3(1.0), a > 0.9 ? 0.5 : 0.1);
        a *= fade;
    } else if (kind == 15) {
        // Shards: hit.y splinters flung out from the middle, each on its own
        // line, turning over as they go and hopping up off the ground, out as
        // hit.x runs. hit.z flattens their spread onto the ground, hit.w is
        // how big they are.
        float prog = fx.hit.x, n = max(fx.hit.y, 3.0), squash = max(fx.hit.z, 0.2), size = max(fx.hit.w, 0.2);
        vec2 p = q * 2.0 - 1.0;
        float go = 1.0 - (1.0 - prog) * (1.0 - prog);
        for (int i = 0; i < 20; ++i) {
            if (float(i) >= n) break;
            float h = Hash(vec2(float(i), 3.0) + fx.kind.w), h2 = Hash(vec2(float(i), 11.0) + fx.kind.w);
            float at = (float(i) + 0.6 * (h - 0.5)) / n * 6.2831853;
            float dist = (0.12 + 0.8 * go) * (0.55 + 0.45 * h2);
            vec2 centre = vec2(cos(at), sin(at) * squash) * dist;
            centre.y -= sin(prog * 3.14159265) * 0.25 * h;
            float spinA = at + prog * (6.0 + 8.0 * h) * (h2 > 0.5 ? 1.0 : -1.0);
            vec2 d = p - centre;
            vec2 local = vec2(dot(d, vec2(cos(spinA), sin(spinA))), dot(d, vec2(-sin(spinA), cos(spinA))));
            float hl = 0.075 * size, hw = 0.03 * size;
            if (abs(local.x) < hl && abs(local.y) < hw * (1.0 - abs(local.x) / hl * 0.7)) {
                a = 0.95 * (1.0 - smoothstep(0.55, 1.0, prog));
                rgb = mix(c, vec3(1.0), 0.35 * (1.0 - prog));
            }
        }
        a *= fade;
    } else if (kind == 16) {
        // A wave: a front of force hit.w thick running out from the middle as
        // hit.z runs, across hit.y radians either side of angle hit.x (pi or
        // more is a whole ring), brightest at its leading edge, with two
        // fainter ripples behind. Always lying on the ground.
        float ang = fx.hit.x, half_w = fx.hit.y, prog = fx.hit.z, thick = max(fx.hit.w, 0.02);
        vec2 p = q * 2.0 - 1.0;
        p.y /= 0.55;
        float r = length(p);
        float off = abs(mod(atan(p.y, p.x) - ang + 3.14159265, 6.2831853) - 3.14159265);
        float cone = half_w >= 3.1 ? 1.0 : 1.0 - smoothstep(half_w * 0.75, half_w, off);
        float front = 0.12 + 0.88 * (1.0 - (1.0 - prog) * (1.0 - prog));
        for (int i = 0; i < 3; ++i) {
            float at = front - float(i) * thick * 1.6;
            float d = r - at;
            float th = thick * (i == 0 ? 1.0 : 0.5);
            if (d > -th && d < th * 0.4) {
                float v = (1.0 - abs(d) / th) * (i == 0 ? 1.0 : 0.45) * cone * (1.0 - prog * 0.7);
                if (v > a) { a = v; rgb = mix(c, vec3(1.0), i == 0 ? 0.5 * (1.0 - abs(d) / th) : 0.0); }
            }
        }
        a *= fade;
    } else {
        vec2 p = q * 2.0 - 1.0;
        float r = length(p);
        a = r < 1.0 ? (1.0 - r) * (1.0 - r) : 0.0;
        a *= fade;
    }

    a *= fx.colour.a;
    o_color = vec4(rgb, clamp(a, 0.0, 1.0)) * v_color;
}
