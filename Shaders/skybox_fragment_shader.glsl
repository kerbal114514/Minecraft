#version 120
varying vec3 v_vertex_pos;
uniform vec3 u_position;
vec4 get_sky_color(float factor)
{
    vec4 skyColor = vec4(0.48, 0.72, 0.93, 1.0);
    vec4 horizonColor = vec4(0.7, 0.9, 1.0, 1.0);
    vec4 final_color;
    if (factor > 0.4)
        final_color = skyColor;
    else
        final_color = mix(horizonColor, skyColor, factor * 2.5);
    return final_color;
}
void main()
{
    float height = normalize(v_vertex_pos - u_position).y;
    float factor = clamp(height, 0.0, 1.0);
    gl_FragColor = get_sky_color(factor);
}
