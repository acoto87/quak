#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec3 a_norm;
uniform mat4 u_proj;
uniform mat4 u_view;
uniform mat4 u_model;
uniform mat3 u_nmat;
uniform vec3 u_color;
out vec3 v_color;
void main() {
    vec3 wn  = normalize(u_nmat * a_norm);
    vec3 sun = normalize(vec3(0.5, 1.0, 0.3));
    float d  = max(dot(wn, sun), 0.0);
    v_color  = u_color * (0.35 + d * 0.65);
    gl_Position = u_proj * u_view * u_model * vec4(a_pos, 1.0);
}
