#include "common.hlsl"

struct VSInput
{
    float3 pos : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 local    : TEXCOORD0;
};

cbuffer CameraCB : register(b0, space1)
{
    CameraUniform camera;
}

cbuffer TransformCB : register(b1, space1)
{
    TransformUniform transform;
}

VSOutput main(VSInput input)
{
    VSOutput output;
    output.local = input.pos.xz;
    output.position = mul(camera.u_proj,
                          mul(camera.u_view,
                              mul(transform.u_model, float4(input.pos, 1.0))));
    return output;
}
