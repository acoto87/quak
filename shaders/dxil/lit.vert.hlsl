#include "common.hlsl"

struct VSInput
{
    float3 pos  : TEXCOORD0;
    float3 norm : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

cbuffer CameraCB : register(b0, space1)
{
    CameraUniform camera;
}

cbuffer TransformCB : register(b1, space1)
{
    TransformUniform transform;
}

cbuffer ColorCB : register(b2, space1)
{
    float4 u_color;
}

cbuffer EnvironmentCB : register(b3, space1)
{
    EnvironmentUniform environment;
}

VSOutput main(VSInput input)
{
    VSOutput output;
    float3 wn = normalize(transform.u_nmat[0].xyz * input.norm.x
                        + transform.u_nmat[1].xyz * input.norm.y
                        + transform.u_nmat[2].xyz * input.norm.z);
    float d = max(dot(wn, SUN_DIR), 0.0);
    float day_mix = environment.u_environment.x;
    float ambient = lerp(0.18, 0.35, day_mix);
    float directional = lerp(0.20, 0.65, day_mix);
    output.color = float4(u_color.rgb * (ambient + d * directional), u_color.a);
    output.position = mul(camera.u_proj,
                          mul(camera.u_view,
                              mul(transform.u_model, float4(input.pos, 1.0))));
    return output;
}
