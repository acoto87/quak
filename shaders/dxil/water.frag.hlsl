#include "common.hlsl"

struct PSInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float3 world    : TEXCOORD1;
    float3 normal   : TEXCOORD2;
};

cbuffer WaterFragCB : register(b0, space3)
{
    float4 u_time_pad;
}

cbuffer EnvironmentCB : register(b1, space3)
{
    EnvironmentUniform environment;
}

float4 main(PSInput input) : SV_Target0
{
    float time = u_time_pad.x;
    float day_mix = environment.u_environment.x;
    float3 shallow = lerp(float3(0.025, 0.12, 0.25),
                          float3(0.05, 0.35, 0.75), day_mix);
    float3 deep = lerp(float3(0.01, 0.045, 0.16),
                       float3(0.02, 0.18, 0.50), day_mix);
    float3 col = lerp(shallow, deep,
                      saturate(0.5 - input.world.y * 2.0));
    col += 0.04 * sin(input.uv.x * 14.0 + time * 2.8) * sin(input.uv.y * 11.0 + time * 2.1);

    float3 eyev = normalize(float3(0.0, 10.0, 12.0) - input.world);
    float3 h = normalize(SUN_DIR + eyev);
    float specular = lerp(0.10, 0.50, day_mix);
    float sp = pow(max(dot(normalize(input.normal), h), 0.0), 48.0) * specular;

    return float4(saturate(col + sp), 0.88);
}
