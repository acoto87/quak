#pragma pack_matrix(column_major)

struct VSInput
{
    float3 pos : TEXCOORD0;
};

struct CameraUniform
{
    float4x4 u_proj;
    float4x4 u_view;
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

cbuffer ColorCB : register(b1, space1)
{
    float4 u_color;
}

VSOutput main(VSInput input)
{
    VSOutput output;
    output.color = u_color;
    output.position = mul(camera.u_proj, mul(camera.u_view, float4(input.pos, 1.0)));
    return output;
}
