#version 450
// Water that runs -- drawn over every water tile by SDL's GPU renderer.
//
// The tile's own art slides along the current, a texel at a time so it stays
// pixel art, and sways across as it goes; glints ride the current and foam
// laps where the water meets the bank. Where the field says the water is
// still -- a pond, a lake -- it only sways and glitters.
//
// The field is one texel for every sixteen world pixels of the map: green
// where there is water, and in blue and alpha which way it runs and how hard
// (see BuildField in src/systems/shaders.cpp). Everything is worked out in
// whole world pixels, from the fragment's place on screen and the camera, so
// the pattern runs on unbroken from one tile to the next.

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

// One texel of the tile's art, wrapped: water tiles are drawn to tile.
vec4 Texel(vec2 uv, vec2 size) {
    vec2 tc = mod(floor(uv * size), size);
    return texture(u_texture, (tc + 0.5) / size);
}

void main() {
    float t = view.cam.w;
    vec2 size = vec2(textureSize(u_texture, 0));

    // Work in the art's own pixels, not the screen's: a 16px tile is drawn
    // 32 world pixels wide, so each of its pixels is two world pixels, and
    // everything below moves and flickers a whole art pixel at a time. From
    // how fast UV moves across the screen, since the map says how big a tile
    // is drawn, not the texture.
    vec2 uv_per_px = vec2(dFdx(v_uv.x), dFdy(v_uv.y)) * view.cam.z;
    vec2 texel_px = max(1.0 / abs(uv_per_px * size), vec2(1.0));
    vec2 world = gl_FragCoord.xy / view.cam.z - view.cam.xy;
    vec2 cell = floor(world / texel_px);
    vec4 f = texture(u_field, (cell + 0.5) * texel_px / (view.field.x * view.field.yz));
    vec2 flow = f.ba * 2.0 - 1.0;
    float running = clamp(length(flow), 0.0, 1.0);

    // Downstream, and a sway across the current -- or across the rows, on
    // still water. Both in whole art pixels. The sway changes only along the
    // current, so each row across it moves as one and the art's ripples keep
    // their shape.
    vec2 along = running > 0.1 ? flow / running : vec2(0.0, 1.0);
    vec2 across = vec2(-along.y, along.x);
    float sway = floor(sin(dot(cell, along) * 0.62 + t * 2.3) * 1.2 + 0.5);
    vec2 drift = floor(flow * t * 26.0 / texel_px + across * sway);
    vec4 c = Texel(v_uv - drift / size, size);

    // Glints: the odd bright pixel riding the current, or twinkling in place
    // on still water.
    vec2 g = floor(cell - flow * t * 34.0 / texel_px);
    float still = running > 0.1 ? 0.0 : floor(t * 1.5);
    float glint = step(0.995, Hash(g + still * vec2(17.0, 31.0)));

    // Foam where the water thins toward a bank, broken and moving.
    float edge = 1.0 - smoothstep(0.62, 0.9, f.g);
    float foam = edge * step(0.55, Hash(cell + floor(t * 2.5) * vec2(3.0, 7.0)));

    // Both are the water's own colour, lightened: pale blue on the river,
    // a murky grey-green on the Bayou's bog.
    vec3 light = min(c.rgb + vec3(0.38), vec3(1.0));
    vec3 col = mix(c.rgb, light, max(glint, foam * 0.6));
    o_color = vec4(col, c.a) * v_color;
}
