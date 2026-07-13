#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec3 a_norm;
uniform mat4 u_proj;
uniform mat4 u_view;
uniform mat4 u_model;
uniform mat3 u_nmat;
out vec2 v_uv;
out vec3 v_norm;
void main() {
    v_uv   = a_uv;
    v_norm = normalize(u_nmat * a_norm);
    gl_Position = u_proj * u_view * u_model * vec4(a_pos, 1.0);
}
