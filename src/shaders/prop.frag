#version 450
// Scenery that moves, or that lights up -- every mode of it is the art's own
// pixels, moved whole or kept or dropped, never smeared.
//
//   1 plants     grass, reeds, bushes, trees: the top moves, the foot does not,
//                and gusts roll across a field as a wave rather than the whole
//                field nodding at once; a tree's crown bends with the gust and
//                rustles -- little clusters of its leaves flicking a pixel this
//                way and that, more while a gust goes through
//   2 cloth      banners and tapestries hung from the top, so the free end
//                moves most, with a flutter running down it; given a negative
//                strength, a tent or a banner on a pole, held at both ends
//   3 windows    a building by day: its lit glass is dimmed to glass, so the
//                windows can come on at dusk (the glow pass lights them)
//   4 fountain   the water in a basin or a jet runs and glitters
//   5 pulse      a woken waystone: its runes breathe
//   6 glow       after dark, drawn added over the lit scene: only what is lit
//                from inside -- windows, flames, lava, eyes, crystals -- and
//                nothing else, so those shine through the night
//
// And in the Cozy look (style.x), whatever the mode but the glow: the colour
// softened, the sun on the top edge, and a soft brown line round the thing --
// a shade of what it is beside. Mode 0 is that and nothing else.
//
// A pixel counts as lit from inside when it is both bright and strongly
// coloured: the renders put emission into exactly those, and nothing else in
// the palette is both.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 3, binding = 0) uniform Prop {
    vec4 cam;     // camera xpos, ypos, zoom; seconds
    vec4 mode;    // kind, strength, night 0..1, wind 0..1
    vec4 wind;    // gust direction x, y; gust speed; gust length in world px
    vec4 style;   // the Cozy look: on, its line's strength, colour kept, the sun on its top edge
    vec4 more;    // how much a crown rustles (trees 1); grass parts for walkers (1); two to spare
    vec4 walk0;   // whoever is walking about, world px: x, y of two
    vec4 walk1;   // and of two more (-99999 for nobody)
} prop;

float Hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

vec4 At(vec2 texel, vec2 size) {
    if (texel.x < 0.0 || texel.y < 0.0 || texel.x >= size.x || texel.y >= size.y) return vec4(0.0);
    return texture(u_texture, (texel + 0.5) / size);
}

bool Lit(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
    return mx > 0.9 && mx - mn > 0.33;
}

bool Water(vec3 c) {
    return c.b > 0.42 && c.b > c.r + 0.12 && c.b >= c.g - 0.06;
}

vec3 CozyColour(vec3 c) {
    float l = dot(c, vec3(0.3, 0.59, 0.11));
    c = mix(vec3(l), c, prop.style.z);
    float shade = clamp((0.45 - l) / 0.45, 0.0, 1.0);
    return mix(c, c * vec3(0.92, 0.96, 1.08), shade * 0.6);
}

// `col` is what is drawn at `texel`, whose own alpha is `a0` (before any fade).
vec4 Finish(vec4 col, float a0, vec2 texel, vec2 size) {
    if (prop.style.x < 0.5) return col;
    if (a0 > 0.5) {
        col.rgb = CozyColour(col.rgb);
        if (texel.y >= 1.0 && At(texel - vec2(0.0, 1.0), size).a < 0.5) col.rgb = min(col.rgb * prop.style.w + 0.02, vec3(1.0));
        return col;
    }
    vec2 dirs[4] = vec2[](vec2(1, 0), vec2(-1, 0), vec2(0, 1), vec2(0, -1));
    for (int i = 0; i < 4; ++i) {
        vec4 c = At(texel + dirs[i], size);
        if (c.a > 0.5) return vec4(c.rgb * v_color.rgb * 0.3 + vec3(0.196, 0.125, 0.078), prop.style.y * v_color.a);
    }
    return col;
}

void main() {
    vec2 size = vec2(textureSize(u_texture, 0));
    vec2 texel = floor(v_uv * size);
    float t = prop.cam.w;
    int kind = int(prop.mode.x + 0.5);
    float strength = prop.mode.y;
    float night = prop.mode.z;
    vec2 world = gl_FragCoord.xy / prop.cam.z - prop.cam.xy;

    if (kind == 1 || kind == 2) {
        // Where the thing stands, the same for every pixel of it, so each row
        // of it moves as one: a gust worked out per pixel tears it apart.
        vec2 uv_per_px = vec2(dFdx(v_uv.x), dFdy(v_uv.y)) * prop.cam.z;
        vec2 texel_px = max(1.0 / abs(uv_per_px * size), vec2(0.25));
        vec2 foot = floor(world - v_uv * size * texel_px + vec2(0.5, 1.0) * size * texel_px + 0.5);
        // A gust is a wave across the ground; the breeze is always there.
        vec2 dir = normalize(prop.wind.xy);
        float gust = 0.5 + 0.5 * sin(dot(foot, dir) / prop.wind.w * 6.2831853 - t * prop.wind.z);
        float breeze = sin(t * 1.7 + foot.x * 0.013 + foot.y * 0.009);
        float held = strength < 0.0 ? 1.0 : 0.0;
        strength = abs(strength);
        float sway = (breeze * 0.35 + gust * gust) * prop.mode.w * strength;
        float amount;
        float flutter = 0.0;
        if (kind == 1) {
            float h = 1.0 - v_uv.y;                    // the foot of the art is its root
            amount = h * h * 2.2;
        } else {
            // Hung from the top, or held at the top and the foot.
            float middle = 4.0 * v_uv.y * (1.0 - v_uv.y);
            float free = held > 0.5 ? middle * middle : v_uv.y;
            amount = free * 2.0;
            flutter = sin(texel.y * 0.7 - t * 8.0 + foot.x * 0.05) * 0.8 * free * prop.mode.w;
        }
        float shift = floor(sway * amount + flutter * strength + 0.5);
        vec2 from = texel - vec2(shift * sign(dir.x), 0.0);
        if (prop.more.y > 0.0) {
            // Walked through: the tuft leans away from whoever is in it, its top
            // most, and straightens again behind them.
            vec2 ws[4] = vec2[](prop.walk0.xy, prop.walk0.zw, prop.walk1.xy, prop.walk1.zw);
            float push = 0.0;
            for (int i = 0; i < 4; ++i) {
                vec2 d = foot - ws[i];
                float near = 1.0 - clamp(length(d * vec2(1.0, 1.8)) / 26.0, 0.0, 1.0);
                push += (d.x >= 0.0 ? 1.0 : -1.0) * near;
            }
            float h = 1.0 - v_uv.y;
            from.x -= floor(clamp(push, -1.0, 1.0) * 5.0 * h * h + 0.5 * sign(push));
        }
        if (prop.more.x > 0.0) {
            // The crown rustles, not the trunk: clusters of three texels flick
            // over a pixel and back, each on its own beat, more in a gust.
            float crown = clamp((0.85 - v_uv.y) / 0.5, 0.0, 1.0);
            float stir = (0.25 + gust * gust) * prop.mode.w * prop.more.x * crown;
            vec2 cluster = floor(texel / 3.0);
            float beat = Hash(cluster + vec2(floor(t * 7.0 + Hash(cluster) * 7.0), 0.0));
            if (beat < stir * 0.55) from.x += Hash(cluster + 17.0) > 0.5 ? 1.0 : -1.0;
            float lift = Hash(cluster + vec2(0.0, floor(t * 5.0 + Hash(cluster + 3.0) * 5.0)));
            if (lift < stir * 0.25) from.y += 1.0;
        }
        vec4 there = At(from, size);
        o_color = Finish(there * v_color, there.a, from, size);
        return;
    }

    vec4 base = At(texel, size);
    vec4 col = base * v_color;

    if (kind == 3) {
        // By day the windows are glass; the glow pass lights them after dark.
        if (Lit(col.rgb)) {
            vec3 glass = vec3(0.22, 0.25, 0.33) + col.rgb * 0.22;
            col.rgb = mix(col.rgb, glass, (1.0 - night) * 0.7);
        }
    } else if (kind == 4) {
        // Water in a basin: it runs down a pixel at a time, and glitters.
        if (Water(col.rgb)) {
            float step = floor(fract(t * 2.2 + texel.x * 0.37) * 3.0);
            vec4 above = At(texel - vec2(0.0, step), size) * v_color;
            if (Water(above.rgb)) col.rgb = above.rgb;
            if (Hash(texel + floor(t * 5.0)) > 0.96) col.rgb = mix(col.rgb, vec3(0.92, 0.98, 1.0), 0.8);
        }
    } else if (kind == 5) {
        if (Lit(col.rgb) || col.b > 0.8) col.rgb *= 0.82 + 0.3 * (0.5 + 0.5 * sin(t * 2.4));
    } else if (kind == 6) {
        // Only what is lit from inside, added over the night.
        if (!Lit(col.rgb) || col.a < 0.5) { o_color = vec4(0.0); return; }
        float flicker = 0.9 + 0.1 * sin(t * 7.0 + Hash(floor(world / 24.0)) * 6.28);
        o_color = vec4(col.rgb * strength * night * flicker, col.a);
        return;
    }
    o_color = Finish(col, base.a, texel, size);
}
