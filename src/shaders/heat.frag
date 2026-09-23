#version 450
// The air over lava, wavering -- drawn once over a whole view.
//
// When there is lava in view the world is drawn into a texture first, and this
// draws that texture back to the screen shifted a pixel sideways, row
// by row, wherever there is lava under the air -- or a little way below it,
// since heat rises. Everything standing in that air wavers with it: the
// people, the palace's torches, the far bank. Anywhere else the shift is
// nothing and the pixel is the world's own.
//
// Whole world pixels again, so the wavering is a pixel-art ripple and not a
// blur. The field's red is where the lava is (see BuildField).

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;   // the world, drawn
layout(set = 2, binding = 1) uniform sampler2D u_field;
layout(set = 3, binding = 0) uniform View {
    vec4 cam;      // camera xpos, ypos, zoom; seconds
    vec4 field;    // world px per field texel; the field's width and height in texels
    vec4 target;   // the size in pixels of the view being drawn
} view;

void main() {
    float t = view.cam.w;
    vec2 px = v_uv * view.target.xy;
    vec2 world = px / view.cam.z - view.cam.xy;

    float heat = 0.0;
    for (int k = 0; k < 5; ++k) {
        vec2 below = world + vec2(0.0, 12.0 * float(k));
        float lava = texture(u_field, below / (view.field.x * view.field.yz)).r;
        heat = max(heat, lava * (1.0 - 0.17 * float(k)));
    }
    heat = smoothstep(0.1, 0.8, heat);

    float row = floor(world.y);
    float wave = sin(row * 0.55 + t * 6.0) * 0.65
               + sin(row * 0.21 - t * 3.5 + floor(world.x / 6.0) * 1.3) * 0.35;
    // One world pixel either way at most: a haze, not a wobble.
    float shift = floor(wave * heat * 1.2 + 0.5);
    vec2 uv = v_uv + vec2(shift * view.cam.z / view.target.x, 0.0);
    o_color = texture(u_texture, uv) * v_color;
}
