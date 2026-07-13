#pragma pack_matrix(column_major)

struct CameraUniform
{
    float4x4 u_proj;
    float4x4 u_view;
};

struct TransformUniform
{
    float4x4 u_model;
    float4 u_nmat[3];
};

struct WaterUniform
{
    float4 u_time_pad;
    float4 u_ripple_data[32];
};

struct EnvironmentUniform
{
    float4 u_environment;
};

static const float3 SUN_DIR = normalize(float3(0.5, 1.0, 0.3));
