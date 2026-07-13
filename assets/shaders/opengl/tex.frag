#version 330 core
in vec2 v_uv;
in vec3 v_norm;
uniform sampler2D u_tex;
out vec4 frag;
void main() {
    vec3 sun = normalize(vec3(0.5, 1.0, 0.3));
    float d  = max(dot(v_norm, sun), 0.0);
    vec4 c   = texture(u_tex, v_uv);
    frag     = vec4(c.rgb * (0.4 + d * 0.6), 1.0);
}
