#version 130
precision mediump float;
uniform sampler2DArray u_texture_array;
varying vec3 v_tex_coords;
varying float v_distance;
uniform float u_render_distance;
void main() {
    if (v_distance > u_render_distance) discard;
    vec4 color = texture(u_texture_array, v_tex_coords);
    if (color.a < 0.5)
        discard;
    if (u_render_distance - v_distance < 16.0)
        color = mix(vec4(0.5, 0.69, 1.0, 1.0), color, (u_render_distance - v_distance) / 16.0);
    gl_FragColor = color;
}
