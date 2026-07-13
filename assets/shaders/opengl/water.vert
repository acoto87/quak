#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;
uniform mat4  u_proj;
uniform mat4  u_view;
uniform float u_time;
uniform vec2  u_rip_pos[32];
uniform float u_rip_rad[32];
uniform float u_rip_alpha[32];
out vec2 v_uv;
out vec3 v_world;
out vec3 v_norm;
void main() {
    vec3 p = a_pos;
    float w = 0.12*sin(p.x*0.8 + u_time*1.4)
            + 0.07*sin(p.z*1.1 + u_time*1.0)
            + 0.04*sin(p.x*1.5 - p.z*0.9 + u_time*2.1);
    for (int i=0; i<32; i++) {
        if (u_rip_alpha[i] < 0.01) continue;
        float dx = p.x - u_rip_pos[i].x;
        float dz = p.z - u_rip_pos[i].y;
        float d  = sqrt(dx*dx + dz*dz);
        float df = abs(d - u_rip_rad[i]);
        if (df < 2.0) w += u_rip_alpha[i]*(1.0-df/2.0)*0.18*sin(d*2.5-u_time*7.0);
    }
    p.y += w;
    float dydx = 0.12*0.8*cos(p.x*0.8+u_time*1.4)
               + 0.04*1.5*cos(p.x*1.5-p.z*0.9+u_time*2.1);
    float dydz = 0.07*1.1*cos(p.z*1.1+u_time*1.0)
               - 0.04*0.9*cos(p.x*1.5-p.z*0.9+u_time*2.1);
    v_norm  = normalize(vec3(-dydx, 1.0, -dydz));
    v_uv    = a_uv;
    v_world = p;
    gl_Position = u_proj * u_view * vec4(p, 1.0);
}
