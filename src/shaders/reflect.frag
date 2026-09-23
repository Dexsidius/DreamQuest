#version 450
// What stands at the water's edge, seen in it: drawn upside down under its
// own feet, and shown only where the field says there is water there -- dimmed,
// pulled toward the water's colour, and wobbling a pixel either way down the
// rows as the surface moves. Over lava the same, faint and red: the palace's
// towers glow in their moat.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 2, binding = 1) uniform sampler2D u_field;
layout(set = 3, binding = 0) uniform View {
    vec4 cam;      // camera xpos, ypos, zoom; seconds
    vec4 field;    // world px per field texel; the field's width and height in texels
    vec4 target;
} view;

void main() {
    float t = view.cam.w;
    vec2 world = gl_FragCoord.xy / view.cam.z - view.cam.xy;
    vec4 f = texture(u_field, floor(world) / (view.field.x * view.field.yz));
    float water = step(0.5, f.g), lava = step(0.5, f.r);
    if (water + lava < 0.5) { o_color = vec4(0.0); return; }

    vec2 size = vec2(textureSize(u_texture, 0));
    float row = floor(world.y);
    float wobble = floor(sin(row * 0.8 + t * 3.1) * 1.2 + 0.5);
    vec2 texel = floor(v_uv * size) + vec2(wobble, 0.0);
    vec4 c = texture(u_texture, (texel + 0.5) / size) * v_color;
    if (water > 0.5) {
        c.rgb = mix(c.rgb * 0.7, vec3(0.16, 0.3, 0.42), 0.45);
        c.a *= 0.42;
    } else {
        // Lava is brighter than anything that could be seen in it: what shows
        // is a shape darker than the lava, with its lit windows still lit.
        float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
        bool lit = mx > 0.9 && mx - mn > 0.33;
        c.rgb = lit ? mix(c.rgb, vec3(1.0, 0.85, 0.5), 0.3) : c.rgb * 0.25 + vec3(0.16, 0.03, 0.0);
        c.a *= lit ? 0.7 : 0.38;
    }
    o_color = c;
}
