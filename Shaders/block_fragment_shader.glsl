#version 130
precision mediump float;
uniform sampler2DArray u_texture_array;
varying vec3 v_tex_coords;
varying float v_distance;
varying vec3 v_vertex_pos;
uniform float u_render_distance;
uniform vec3 u_position;
vec4 get_sky_color(float factor)
{
    vec4 skyColor = vec4(0.48, 0.72, 0.93, 1.0);
    vec4 horizonColor = vec4(0.7, 0.9, 1.0, 1.0);
    vec4 final_color;
    if (factor > 0.2)
        final_color = skyColor;
    else
        final_color = mix(horizonColor, skyColor, factor * 5.0);
    return final_color;
}
void main()
{
    if (v_distance > u_render_distance) discard;
    vec4 color = texture(u_texture_array, v_tex_coords);
    if (color.a < 0.5)
        discard;
    if (u_render_distance - v_distance < 8.0)
    {
        float height = normalize(v_vertex_pos - u_position).y;
        float factor = clamp(height, 0.0, 1.0);
        color = mix(get_sky_color(factor), color, (u_render_distance - v_distance) / 8.0);
    }
    gl_FragColor = color;
}
