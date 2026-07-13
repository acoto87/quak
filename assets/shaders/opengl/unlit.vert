#version 330 core
layout(location=0) in vec3 a_pos;
uniform mat4 u_proj;
uniform mat4 u_view;
uniform vec4 u_color;
out vec4 v_color;
void main() {
    v_color = u_color;
    gl_Position = u_proj * u_view * vec4(a_pos, 1.0);
    gl_PointSize = 8.0;
}
