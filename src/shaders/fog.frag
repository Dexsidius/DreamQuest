#version 450
// Ground fog: drawn over the floor and under everyone standing on it, as one
// quad across the view. Drifting banks of it, thicker over and beside water,
// and -- where a map gives one -- only inside a region with a soft edge (the
// graveyard, on the Hollowmarch). In whole world pixels, stepped into a few
// shades, so it is a pixel-art fog and not a gradient.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 2, binding = 1) uniform sampler2D u_field;
layout(set = 3, binding = 0) uniform Fog {
    vec4 cam;      // camera xpos, ypos, zoom; seconds
    vec4 field;    // world px per field texel; field width, height; 1 if there is a field
    vec4 look;     // fog rgb; density
    vec4 region;   // world x0, y0, x1, y1 (all 0: everywhere)
    vec4 more;     // how much water thickens it; drift speed; 0; 0
} fog;

float Hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float Noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(Hash(i), Hash(i + vec2(1, 0)), f.x), mix(Hash(i + vec2(0, 1)), Hash(i + vec2(1, 1)), f.x), f.y);
}

void main() {
    float t = fog.cam.w;
    vec2 world = floor(gl_FragCoord.xy / fog.cam.z - fog.cam.xy);
    vec2 drift = vec2(t * fog.more.y, t * fog.more.y * 0.3);
    float n = Noise((world + drift) / 90.0) * 0.6 + Noise((world - drift * 1.7) / 37.0) * 0.4;
    float density = fog.look.a;
    if (fog.field.w > 0.5 && fog.more.x > 0.0) {
        float wet = 0.0;
        for (int k = 0; k < 4; ++k) {
            vec2 off = vec2(float(k % 2) * 2.0 - 1.0, float(k / 2) * 2.0 - 1.0) * 20.0;
            wet = max(wet, texture(u_field, (world + off) / (fog.field.x * fog.field.yz)).g);
        }
        density *= 1.0 + wet * fog.more.x;
    }
    if (fog.region.z > fog.region.x) {
        vec2 lo = fog.region.xy, hi = fog.region.zw;
        vec2 in_lo = smoothstep(lo, lo + 160.0, world), in_hi = smoothstep(hi, hi - 160.0, world);
        density *= in_lo.x * in_lo.y * in_hi.x * in_hi.y;
    }
    float a = smoothstep(0.35, 0.85, n) * density;
    a = floor(a * 5.0 + 0.5) / 5.0;           // a few shades, not a gradient
    o_color = vec4(fog.look.rgb, clamp(a, 0.0, 0.85)) * v_color;
}
