#include "common.hlsl"

struct VSInput
{
    float3 pos : TEXCOORD0;
    float2 uv  : TEXCOORD1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
    float3 world    : TEXCOORD1;
    float3 normal   : TEXCOORD2;
};

cbuffer CameraCB : register(b0, space1)
{
    CameraUniform camera;
}

cbuffer WaterCB : register(b1, space1)
{
    WaterUniform water;
}

VSOutput main(VSInput input)
{
    VSOutput output;
    float3 p = input.pos;
    float time = water.u_time_pad.x;

    float w = 0.10 * sin(p.x * 0.7 + time * 1.1)
            + 0.05 * sin(p.z * 0.9 + time * 0.8)
            + 0.025 * sin(p.x * 1.2 - p.z * 0.7 + time * 1.6);

    [unroll]
    for (int i = 0; i < 32; i++) {
        float4 ripple = water.u_ripple_data[i];
        if (ripple.w < 0.01) {
            continue;
        }

        float dx = p.x - ripple.x;
        float dz = p.z - ripple.y;
        float d = sqrt(dx * dx + dz * dz);
        float df = abs(d - ripple.z);
        if (df < 2.0) {
            w += ripple.w * (1.0 - df / 2.0) * 0.12 * sin(d * 2.1 - time * 5.4);
        }
    }

    p.y += w;

    float dydx = 0.10 * 0.7 * cos(p.x * 0.7 + time * 1.1)
               + 0.025 * 1.2 * cos(p.x * 1.2 - p.z * 0.7 + time * 1.6);
    float dydz = 0.05 * 0.9 * cos(p.z * 0.9 + time * 0.8)
               - 0.025 * 0.7 * cos(p.x * 1.2 - p.z * 0.7 + time * 1.6);

    output.normal = normalize(float3(-dydx, 1.0, -dydz));
    output.uv = input.uv;
    output.world = p;
    output.position = mul(camera.u_proj, mul(camera.u_view, float4(p, 1.0)));
    return output;
}
