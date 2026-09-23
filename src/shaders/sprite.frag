#version 450
// A character, with what is happening to it drawn on it rather than tinted
// over it -- used only for a sprite that has something to show; everyone else
// is drawn the plain way.
//
//   the flash of a blow      the sprite goes to white (or the colour of what
//                            hit it) for a few frames: a multiply, which is all
//                            a tint is, can never make a sprite brighter
//   a heavy winding up       a glow round its outline, throbbing as it fills
//   Burn                     flames licking up off its outline
//   Chill / Frozen           the colour gone cold; frozen is cracked ice over
//                            it, with glints in it
//   Electrified              sparks crawling round its outline
//   Poison                   green bubbles rising through it
//   Wet                      a sheen sliding down it, and a drip or two
//   Bleed                    drops running down it
//   dying                    dissolving the way it would: to dust, to embers,
//                            or up into the air
//
// Everything is worked in the sheet's own texels, so it is pixel art and not
// a blur. `frame` is the cell being drawn, in UV, so "up" and "down" mean the
// character's and not the sheet's.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 3, binding = 0) uniform Fx {
    vec4 flash;     // rgb, amount
    vec4 glow;      // outline glow rgb, amount
    vec4 status;    // burn, cold (chill .5, frozen 1), electrified, poison
    vec4 status2;   // wet, bleed, dissolve 0..1, dissolve kind (0 plain, 1 dust, 2 embers, 3 into the air)
    vec4 misc;      // seconds, seed, 1 to draw past the outline (0 for a layer over the body), 0
    vec4 frame;     // the cell drawn, in UV: x, y, w, h
} fx;

float Hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float AlphaAt(vec2 texel, vec2 size) {
    // Inside the cell only: a neighbour across the cell's edge is another
    // frame of the sheet.
    vec2 lo = fx.frame.xy * size, hi = (fx.frame.xy + fx.frame.zw) * size;
    if (texel.x < lo.x || texel.y < lo.y || texel.x >= hi.x || texel.y >= hi.y) return 0.0;
    return texture(u_texture, (texel + 0.5) / size).a;
}

void main() {
    vec2 size = vec2(textureSize(u_texture, 0));
    float t = fx.misc.x;
    float seed = fx.misc.y;

    // Into the air: the whole of it drifts up as it goes.
    vec2 uv = v_uv;
    float dissolve = fx.status2.z;
    int kind = int(fx.status2.w + 0.5);
    if (kind == 3 && dissolve > 0.0) uv.y += dissolve * 0.18 * fx.frame.w;

    vec2 texel = floor(uv * size);
    vec2 local = (uv - fx.frame.xy) / max(fx.frame.zw, vec2(1e-5));   // 0..1 in the cell, y down
    vec4 base = texture(u_texture, (texel + 0.5) / size);
    vec4 col = base * v_color;
    float a0 = base.a;
    bool rim_ok = fx.misc.z > 0.5;

    // The outline: out of the figure but beside it (rim) or in it at its edge.
    float n4 = max(max(AlphaAt(texel + vec2(1, 0), size), AlphaAt(texel - vec2(1, 0), size)),
                   max(AlphaAt(texel + vec2(0, 1), size), AlphaAt(texel - vec2(0, 1), size)));
    float n4min = min(min(AlphaAt(texel + vec2(1, 0), size), AlphaAt(texel - vec2(1, 0), size)),
                      min(AlphaAt(texel + vec2(0, 1), size), AlphaAt(texel - vec2(0, 1), size)));
    bool inside = a0 > 0.5;
    bool rim_out = !inside && n4 > 0.5;
    bool rim_in = inside && n4min < 0.5;
    float below1 = AlphaAt(texel + vec2(0, 1), size), below2 = AlphaAt(texel + vec2(0, 2), size);
    float below3 = AlphaAt(texel + vec2(0, 3), size);
    float below5 = max(AlphaAt(texel + vec2(0, 4), size), AlphaAt(texel + vec2(0, 5), size));

    vec3 add = vec3(0.0);
    float add_a = 0.0;

    // --- a heavy winding up: a glow round its outline, two texels out --------------
    if (fx.glow.a > 0.0 && !inside && rim_ok) {
        float near2 = max(max(AlphaAt(texel + vec2(2, 0), size), AlphaAt(texel - vec2(2, 0), size)),
                          max(AlphaAt(texel + vec2(0, 2), size), AlphaAt(texel - vec2(0, 2), size)));
        float g = rim_out ? 1.0 : (near2 > 0.5 ? 0.55 : 0.0);
        if (g > 0.0) { add = fx.glow.rgb; add_a = max(add_a, g * fx.glow.a); }
    }

    if (inside) {
        // --- Chill and Frozen: the colour gone cold, and ice over it ---------------------
        float cold = fx.status.y;
        if (cold > 0.0) {
            float lum = dot(col.rgb, vec3(0.3, 0.55, 0.15));
            vec3 icy = mix(vec3(lum), vec3(0.72, 0.88, 1.0) * (0.55 + lum), 0.55);
            col.rgb = mix(col.rgb, icy, 0.45 * cold);
            if (cold > 0.9) {
                // Cracks: the edges of a coarse cell pattern, fixed on the creature.
                vec2 c = floor(texel / 5.0);
                vec2 f = texel - c * 5.0;
                float h = Hash(c + seed);
                bool crack = (f.x < 1.0 && h > 0.45) || (f.y < 1.0 && h < 0.55);
                if (crack) col.rgb = mix(col.rgb, vec3(0.93, 0.98, 1.0), 0.55);
                if (rim_in) col.rgb = mix(col.rgb, vec3(0.95, 0.99, 1.0), 0.6);
                if (Hash(texel + floor(t * 3.0)) > 0.985) col.rgb = vec3(1.0);
            }
        }
        // --- Burn: its edge alight, and the whole of it warmer ---------------------------
        if (fx.status.x > 0.0) {
            col.rgb = mix(col.rgb, col.rgb * vec3(1.2, 0.88, 0.7), 0.35 * fx.status.x);
            if (rim_in) col.rgb = mix(col.rgb, vec3(1.0, 0.6, 0.18), fx.status.x * (0.35 + 0.35 * Hash(texel + floor(t * 9.0))));
        }
        // --- Wet: a sheen sliding down it, and its edges catching the light -----------
        if (fx.status2.x > 0.0) {
            float band = fract(local.y * 1.3 + local.x * 0.6 - t * 0.35);
            if (band < 0.09) col.rgb = mix(col.rgb, vec3(0.86, 0.94, 1.0), 0.55 * fx.status2.x);
            col.rgb *= mix(vec3(1.0), vec3(0.8, 0.9, 1.08), fx.status2.x);
            if (rim_in && texel.y < (fx.frame.y + fx.frame.w * 0.6) * size.y)
                col.rgb = mix(col.rgb, vec3(0.8, 0.92, 1.0), 0.4 * fx.status2.x);
        }
        // --- Poison: green bubbles rising through it ------------------------------------
        if (fx.status.w > 0.0) {
            vec2 cell = floor((texel + vec2(0.0, floor(t * 6.0))) / vec2(4.0, 4.0));
            float h = Hash(cell + seed * 3.1);
            vec2 in_cell = mod(texel + vec2(0.0, floor(t * 6.0)), 4.0);
            if (h > 0.84 && in_cell.x == 1.0 && in_cell.y == 1.0)
                col.rgb = mix(col.rgb, vec3(0.62, 1.0, 0.42), 0.85 * fx.status.w);
            if (rim_in) col.rgb = mix(col.rgb, vec3(0.45, 0.85, 0.3), 0.35 * fx.status.w);
        }
        // --- Bleed: drops running down it ---------------------------------------------------
        if (fx.status2.y > 0.0) {
            float column = Hash(vec2(texel.x, seed));
            if (column > 0.82) {
                float run = fract(t * 0.6 + column * 7.0);
                float at = mix(0.2, 0.95, run);
                float off = (local.y - at) * fx.frame.w * size.y;
                // A drop, and the darker streak it leaves behind it.
                if (off > -0.5 && off < 1.5) col.rgb = mix(col.rgb, vec3(0.72, 0.04, 0.06), 0.95 * fx.status2.y);
                else if (off < 0.0 && off > -6.0) col.rgb = mix(col.rgb, vec3(0.45, 0.02, 0.04), 0.45 * fx.status2.y);
            }
        }
        // --- Electrified: a flicker through the whole of it ---------------------------------
        if (fx.status.z > 0.0 && Hash(vec2(floor(t * 14.0), seed)) > 0.8)
            col.rgb = mix(col.rgb, vec3(0.8, 0.95, 1.0), 0.35 * fx.status.z);
    }

    // --- Burn: flames licking up off the top of it ------------------------------------------
    if (fx.status.x > 0.0 && !inside && rim_ok) {
        float lick = floor(Hash(vec2(texel.x, floor(t * 10.0) + seed)) * 6.0);   // 0..5 texels of flame
        bool over = (below1 > 0.5 && lick >= 1.0) || (below2 > 0.5 && lick >= 2.0) || (below3 > 0.5 && lick >= 3.0) ||
                    (below5 > 0.5 && lick >= 4.0);
        if (over || (rim_out && Hash(texel + floor(t * 12.0)) > 0.6)) {
            float hot = below1 > 0.5 ? 1.0 : 0.6;
            add = mix(vec3(1.0, 0.42, 0.08), vec3(1.0, 0.86, 0.3), hot * Hash(texel + floor(t * 9.0)));
            add_a = max(add_a, fx.status.x * (0.55 + 0.4 * hot));
        }
    }
    // --- Electrified: sparks crawling round the outline ---------------------------------------
    if (fx.status.z > 0.0 && (rim_in || (rim_out && rim_ok))) {
        if (Hash(texel * 1.7 + floor(t * 22.0)) > 0.78) {
            if (inside) col.rgb = vec3(0.9, 0.98, 1.0);
            else { add = vec3(0.7, 0.92, 1.0); add_a = max(add_a, fx.status.z); }
        }
    }
    // --- Wet: the odd drop falling off its underside ---------------------------------------
    if (fx.status2.x > 0.0 && !inside && rim_ok) {
        float above = AlphaAt(texel - vec2(0, 1 + floor(fract(t * 1.4 + Hash(vec2(texel.x, seed))) * 5.0)), size);
        if (above > 0.5 && Hash(vec2(texel.x, seed + 1.0)) > 0.86) {
            add = vec3(0.72, 0.86, 1.0); add_a = max(add_a, 0.8 * fx.status2.x);
        }
    }

    // --- a blow: to white, or the colour of what struck it ------------------------------------
    if (inside && fx.flash.a > 0.0) col.rgb = mix(col.rgb, fx.flash.rgb, fx.flash.a);

    // --- dying: dissolving the way it would ---------------------------------------------------
    if (dissolve > 0.0 && (inside || add_a > 0.0)) {
        float n = Hash(floor(texel / 2.0) + seed * 13.0);
        // Dust crumbles from the head down, the air takes it from the feet
        // up, embers eat in from the edges wherever the noise says.
        if (kind == 1) n = mix(n, local.y, 0.55);
        else if (kind == 3) n = mix(n, 1.0 - local.y, 0.6);
        float edge = n - dissolve;
        if (edge < 0.0) { o_color = vec4(0.0); return; }
        if (edge < 0.07 && inside) {
            vec3 lip = kind == 2 ? mix(vec3(1.0, 0.35, 0.05), vec3(1.0, 0.85, 0.35), edge / 0.07)
                     : kind == 1 ? vec3(0.62, 0.58, 0.52)
                     : kind == 3 ? vec3(0.9, 0.94, 1.0)
                                 : col.rgb * 0.5;
            col.rgb = lip;
        }
        if (kind == 3) col.a *= 1.0 - dissolve * 0.5;
    }

    if (add_a > 0.0) {
        if (inside) col.rgb = mix(col.rgb, add, add_a * 0.5);
        else col = vec4(add, add_a * v_color.a);
    }
    o_color = col;
}
