#version 120
varying vec3 v_vertex_pos;
uniform float u_player_sky_light;    // [0, 1]
uniform float u_in_liquid;    // 0表示空气，1表示水
int in_liquid;
vec4 get_sky_color(float factor) {
    if (in_liquid == 1) return vec4(0.0, 0.0, u_player_sky_light * 0.5, 1.0);
    vec4 skyColor = vec4(0.48, 0.72, 0.93, 1.0);
    vec4 horizonColor = vec4(0.7, 0.9, 1.0, 1.0);
    vec4 final_color;
    if (factor > 0.2)
        final_color = skyColor;
    else if (factor < -0.2)
        final_color = horizonColor;
    else
        final_color = mix(horizonColor, skyColor, sin(factor * 5 * 3.1415926535 / 2) / 2 + 0.5);
    return vec4(final_color.rgb * u_player_sky_light, final_color.a);
}
void main() {
    in_liquid = int(floor(u_in_liquid + 0.5));
    float height = (90 - degrees(acos(dot(normalize(v_vertex_pos), vec3(0, 1, 0))))) / 90.0;
    float factor = clamp(height - 0.1, -1.0, 1.0);
    gl_FragColor = get_sky_color(factor);
}
