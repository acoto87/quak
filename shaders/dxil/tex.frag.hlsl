#include "common.hlsl"

struct PSInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float3 normal   : TEXCOORD1;
};

cbuffer AlphaCB : register(b0, space3)
{
    float4 u_alpha;
}

cbuffer EnvironmentCB : register(b1, space3)
{
    EnvironmentUniform environment;
}

Texture2D u_tex : register(t0, space2);
SamplerState u_tex_sampler : register(s0, space2);

float4 main(PSInput input) : SV_Target0
{
    float3 sun = normalize(float3(0.5, 1.0, 0.3));
    float d = max(dot(normalize(input.normal), sun), 0.0);
    float4 c = u_tex.Sample(u_tex_sampler, input.uv);
    float day_mix = environment.u_environment.x;
    float ambient = lerp(0.20, 0.40, day_mix);
    float directional = lerp(0.20, 0.60, day_mix);
    return float4(c.rgb * (ambient + d * directional), c.a * u_alpha.x);
}
