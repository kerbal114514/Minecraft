#version 130
varying vec3 v_tex_coords;
varying float v_distance;
varying vec3 v_vertex_pos;
varying vec3 v_normal;
in vec3 a_pos;
in vec3 a_tex_coords;
in float a_normal;
const vec3 normal[7] = vec3[](
    vec3(0.0, 0.0, 0.0),
    vec3(1.0, 0.0, 0.0),
    vec3(-1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, -1.0, 0.0),
    vec3(0.0, 0.0, 1.0),
    vec3(0.0, 0.0, -1.0)
);
void main()
{
    v_vertex_pos = a_pos;
    if (a_normal > 0.5)
        gl_Position = gl_ModelViewProjectionMatrix * vec4(a_pos, 1.0);
    else
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    v_tex_coords = a_tex_coords;
    vec4 view_pos = gl_ModelViewMatrix * vec4(a_pos, 1.0);
    v_distance = length(view_pos.xyz);
    v_normal = normal[int(floor(a_normal + 0.5))];
}
