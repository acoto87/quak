#include "common.hlsl"

struct VSInput
{
    float3 pos   : TEXCOORD0;
    float2 uv    : TEXCOORD1;
    float4 color : TEXCOORD2;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

cbuffer CameraCB : register(b0, space1)
{
    CameraUniform camera;
}

VSOutput main(VSInput input)
{
    VSOutput output;
    output.uv = input.uv;
    output.color = input.color;
    output.position = mul(camera.u_proj, mul(camera.u_view, float4(input.pos, 1.0)));
    return output;
}
