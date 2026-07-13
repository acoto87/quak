struct PSInput
{
    float4 position : SV_Position;
    float2 local    : TEXCOORD0;
};

cbuffer ShadowCB : register(b0, space3)
{
    float4 u_shadow;
}

float4 main(PSInput input) : SV_Target0
{
    float radius = length(input.local);
    float opacity = 1.0 - smoothstep(0.10, 1.0, radius);
    opacity *= opacity * u_shadow.x;
    return float4(0.015, 0.045, 0.055, opacity);
}
