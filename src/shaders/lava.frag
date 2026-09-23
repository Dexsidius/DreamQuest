#version 450
// Lava that churns -- drawn over every lava tile by SDL's GPU renderer.
//
// Two layers of the tile's own art slide slowly downstream at different
// speeds, bending as they go: the crust rides the slow one, and the fast
// one's hot specks run under it. Only the art's own colours are used. A slow wave of
// heat passes across it, lighting its bright parts up, and now and then a
// bubble swells yellow and is gone.
//
// Downstream is the field's (see water.frag and BuildField): the Ashen Path's
// rivers run south out of the palace's moat, and a pool of it only churns.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 2, binding = 1) uniform sampler2D u_field;
layout(set = 3, binding = 0) uniform View {
    vec4 cam;      // camera xpos, ypos, zoom; seconds
    vec4 field;    // world px per field texel; the field's width and height in texels
    vec4 target;   // the heat pass's; unused here
} view;

// A hash without sin(), which some drivers get badly wrong far from zero.
float Hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec4 Texel(vec2 uv, vec2 size) {
    vec2 tc = mod(floor(uv * size), size);
    return texture(u_texture, (tc + 0.5) / size);
}

void main() {
    float t = view.cam.w;
    vec2 size = vec2(textureSize(u_texture, 0));
    // In the art's own pixels, as in water.frag.
    vec2 uv_per_px = vec2(dFdx(v_uv.x), dFdy(v_uv.y)) * view.cam.z;
    vec2 texel_px = max(1.0 / abs(uv_per_px * size), vec2(1.0));
    vec2 world = gl_FragCoord.xy / view.cam.z - view.cam.xy;
    vec2 cell = floor(world / texel_px);
    vec2 centre = (cell + 0.5) * texel_px;
    vec4 f = texture(u_field, centre / (view.field.x * view.field.yz));
    vec2 flow = f.ba * 2.0 - 1.0;

    // Each layer bends a pixel or so as it goes: a row slides a little
    // against the next, a column against the next.
    vec2 d1 = flow * t * 5.0 / texel_px
            + vec2(sin(cell.y * 0.14 + t * 0.9), cos(cell.x * 0.12 + t * 0.7)) * 1.2;
    vec2 d2 = flow * t * 9.0 / texel_px
            + vec2(cos(cell.y * 0.10 - t * 0.6), sin(cell.x * 0.10 + t * 1.1)) * 1.6
            + vec2(5.0, 11.0);
    // The crust is the slow layer's; the fast one only shows its hot specks,
    // running under it.
    vec4 a = Texel(v_uv - floor(d1) / size, size);
    vec4 b = Texel(v_uv - floor(d2) / size, size);
    vec4 c = (Luma(b.rgb) > 0.7 && Luma(a.rgb) < 0.7) ? b : a;

    // A slow wave of heat across it, in four steps, lighting what is already
    // bright; the plain orange barely moves.
    float pulse = floor((0.5 + 0.5 * sin(t * 1.7 + centre.x * 0.045 + centre.y * 0.035)) * 4.0) / 4.0;
    float hot = smoothstep(0.45, 0.85, Luma(c.rgb));
    c.rgb += hot * pulse * vec3(0.20, 0.10, 0.02);

    // Now and then a bubble: two art pixels square, yellow, gone.
    vec2 blob = floor(cell / 2.0);
    float life = fract(t * 0.35 + Hash(blob) * 13.0);
    float bubble = step(0.93, Hash(blob + 7.1)) * step(life, 0.12);
    c.rgb = mix(c.rgb, vec3(1.0, 0.86, 0.42), bubble * 0.8);

    o_color = c * v_color;
}
