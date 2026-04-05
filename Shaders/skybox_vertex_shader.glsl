#version 120
varying vec3 v_vertex_pos;
void main() {
    v_vertex_pos = gl_Vertex.xyz;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
