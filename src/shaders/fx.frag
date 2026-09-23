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
    } else {
        vec2 p = q * 2.0 - 1.0;
        float r = length(p);
        a = r < 1.0 ? (1.0 - r) * (1.0 - r) : 0.0;
        a *= fade;
    }

    a *= fx.colour.a;
    o_color = vec4(rgb, clamp(a, 0.0, 1.0)) * v_color;
}
