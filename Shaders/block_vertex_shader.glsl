#version 130
varying vec3 v_tex_coords;
varying float v_distance;
void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    v_tex_coords = gl_MultiTexCoord0.xyz;
    vec4 view_pos = gl_ModelViewMatrix * gl_Vertex;
    v_distance = length(view_pos.xyz);
}
