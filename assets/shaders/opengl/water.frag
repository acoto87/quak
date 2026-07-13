#version 330 core
in vec2 v_uv;
in vec3 v_world;
in vec3 v_norm;
uniform float u_time;
out vec4 frag;
void main() {
    vec3 col = mix(vec3(0.05,0.35,0.75), vec3(0.02,0.18,0.50),
                   clamp(0.5 - v_world.y*2.0, 0.0, 1.0));
    col += 0.04*sin(v_uv.x*14.0+u_time*2.8)*sin(v_uv.y*11.0+u_time*2.1);
    vec3 sun  = normalize(vec3(0.5, 1.0, 0.3));
    vec3 eyev = normalize(vec3(0.0, 10.0, 12.0) - v_world);
    vec3 h    = normalize(sun + eyev);
    float sp  = pow(max(dot(normalize(v_norm), h), 0.0), 48.0) * 0.5;
    frag = vec4(clamp(col + sp, 0.0, 1.0), 0.88);
}
