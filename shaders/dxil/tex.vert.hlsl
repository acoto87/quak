#include "common.hlsl"

struct VSInput
{
    float3 pos  : TEXCOORD0;
    float2 uv   : TEXCOORD1;
    float3 norm : TEXCOORD2;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float3 normal   : TEXCOORD1;
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
    output.uv = input.uv;
    output.normal = normalize(transform.u_nmat[0].xyz * input.norm.x
                            + transform.u_nmat[1].xyz * input.norm.y
                            + transform.u_nmat[2].xyz * input.norm.z);
    output.position = mul(camera.u_proj,
                          mul(camera.u_view,
                              mul(transform.u_model, float4(input.pos, 1.0))));
    return output;
}
