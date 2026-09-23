#version 450
// The world, drawn once into a texture, put on the screen through this: what
// bends it, what colours it, and what flashes over it. It grew out of the heat
// shader and still does exactly what that did, over lava, among the rest.
//
//   heat          the air over lava wavers, a pixel either way; so does the air
//                 round a forge, a brazier or a fireball going past
//   shockwaves    a meteor landing, a slab dropped, a bolt from the sky, a
//                 boss's slam: a ring that pushes the picture outward as it goes
//   the dream     the edges of the Reverie swim, and bright things bloom
//   fringing      the colours come apart a little at the edges, in a dream and
//                 in a shockwave (a setting of its own: some people hate it)
//   grading       each place and hour its own colour: the Ashen Path red,
//                 the Ice Spire cold, dusk warm, the dead of night drained
//   a flash       lightning, a waystone waking
//
// Every bend is rounded to whole world pixels, as the heat always was, so it
// moves pixel art rather than smearing it.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;   // the world, drawn
layout(set = 2, binding = 1) uniform sampler2D u_field;     // where the lava is
layout(set = 3, binding = 0) uniform Post {
    vec4 cam;        // camera xpos, ypos, zoom; seconds
    vec4 field;      // world px per field texel; field width, height; 1 if there is a field
    vec4 target;     // the view's size in pixels
    vec4 opts;       // distortion on, fringing on, dream strength, lava heat on
    vec4 grade;      // rgb multiply; saturation
    vec4 grade2;     // contrast, lift, 0, 0
    vec4 flash;      // rgb, amount
    vec4 shocks[4];  // centre x, y in view pixels; radius in view pixels; strength
    vec4 heats[8];   // world x, y; radius in world px; strength
} post;

void main() {
    float t = post.cam.w;
    float zoom = post.cam.z;
    vec2 px = v_uv * post.target.xy;
    vec2 world = px / zoom - post.cam.xy;
    bool bend = post.opts.x > 0.5;

    // --- heat: over the lava, and round whatever else is hot -------------------------------
    float heat = 0.0;
    if (bend && post.opts.w > 0.5 && post.field.w > 0.5) {
        for (int k = 0; k < 5; ++k) {
            vec2 below = world + vec2(0.0, 12.0 * float(k));
            float lava = texture(u_field, below / (post.field.x * post.field.yz)).r;
            heat = max(heat, lava * (1.0 - 0.17 * float(k)));
        }
        heat = smoothstep(0.1, 0.8, heat);
    }
    if (bend) {
        for (int i = 0; i < 8; ++i) {
            vec4 h = post.heats[i];
            if (h.w <= 0.0) continue;
            // Heat rises: the air above the thing wavers more than beside it.
            vec2 d = world - h.xy;
            d.y = d.y > 0.0 ? d.y * 2.0 : d.y * 0.6;
            heat = max(heat, h.w * (1.0 - smoothstep(h.z * 0.35, h.z, length(d))));
        }
    }
    float row = floor(world.y);
    float wave = sin(row * 0.55 + t * 6.0) * 0.65
               + sin(row * 0.21 - t * 3.5 + floor(world.x / 6.0) * 1.3) * 0.35;
    vec2 offset = vec2(floor(wave * heat * 1.2 + 0.5) * zoom, 0.0);

    // --- shockwaves -----------------------------------------------------------------------
    float shock_fringe = 0.0;
    if (bend) {
        for (int i = 0; i < 4; ++i) {
            vec4 s = post.shocks[i];
            if (s.w <= 0.0) continue;
            vec2 d = px - s.xy;
            float dist = length(d);
            float band = (dist - s.z) / (10.0 * zoom);
            float ring = exp(-band * band) * s.w;
            if (dist > 0.5) offset += d / dist * ring * 5.0 * zoom;
            shock_fringe += ring;
        }
    }

    // --- the dream: its edges swim ------------------------------------------------------------
    float dream = post.opts.z;
    if (bend && dream > 0.0) {
        float edge = smoothstep(0.35, 1.0, length((v_uv - 0.5) * 2.0));
        offset += vec2(sin(px.y / zoom * 0.07 + t * 1.3), cos(px.x / zoom * 0.06 + t * 1.1)) * dream * edge * 2.5 * zoom;
    }

    offset = floor(offset / zoom + 0.5) * zoom;
    vec2 uv = v_uv + offset / post.target.xy;
    vec4 col = texture(u_texture, uv);

    // --- fringing --------------------------------------------------------------------------------
    float fringe = post.opts.y > 0.5 ? (dream * 1.2 + shock_fringe * 1.5) : 0.0;
    if (fringe > 0.01) {
        vec2 dir = (v_uv - 0.5);
        vec2 k = dir * fringe * 0.012;
        col.r = texture(u_texture, uv + k).r;
        col.b = texture(u_texture, uv - k).b;
    }

    // --- the dream's bloom: bright things spill a little -------------------------------------
    if (dream > 0.0) {
        vec3 spill = vec3(0.0);
        for (int i = 0; i < 8; ++i) {
            float a = 6.2831853 * float(i) / 8.0;
            vec2 o = vec2(cos(a), sin(a)) * 3.0 * zoom / post.target.xy;
            spill += max(texture(u_texture, uv + o).rgb - 0.55, 0.0);
        }
        col.rgb += spill * 0.16 * dream;
    }

    // --- grading ------------------------------------------------------------------------------------
    col.rgb *= post.grade.rgb;
    float lum = dot(col.rgb, vec3(0.299, 0.587, 0.114));
    col.rgb = mix(vec3(lum), col.rgb, post.grade.a);
    col.rgb = (col.rgb - 0.5) * post.grade2.x + 0.5 + post.grade2.y;

    // --- a flash ------------------------------------------------------------------------------------
    col.rgb = mix(col.rgb, post.flash.rgb, post.flash.a);

    o_color = vec4(clamp(col.rgb, 0.0, 1.0), col.a) * v_color;
}
