struct PSInput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target0
{
    float2 p = input.uv * 2.0 - 1.0;
    float taper = lerp(0.72, 1.0, saturate(input.uv.y));
    p.x /= taper;

    float radius_sq = dot(p, p);
    float coverage = 1.0 - smoothstep(0.62, 1.0, radius_sq);
    float highlight = (1.0 - smoothstep(0.02, 0.28,
                                       length(p - float2(-0.28, 0.30)))) * 0.28;
    float3 color = lerp(input.color.rgb, float3(1.0, 1.0, 1.0), highlight);
    return float4(color, input.color.a * coverage);
}
