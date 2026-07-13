struct PSInput
{
    float4 position : SV_Position;
    float4 color    : TEXCOORD0;
};

cbuffer AlphaCB : register(b0, space3)
{
    float4 u_alpha;
}

float4 main(PSInput input) : SV_Target0
{
    return float4(input.color.rgb, input.color.a * u_alpha.x);
}
