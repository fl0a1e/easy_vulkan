[[vk::binding(0, 0)]]
cbuffer CameraData
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewInv;
    float4x4 projInv;
    float3 cameraPosition;
    float _pad0;
};

struct ModelData
{
    float4x4 model;
    float4 meshInfo;
};

[[vk::push_constant]]
ModelData pushConstants;

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float3 Normal : NORMAL;
    [[vk::location(2)]] float2 UV : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 WorldNormal : NORMAL;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
    [[vk::location(2)]] nointerpolation float HasTexcoord : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPosition = mul(pushConstants.model, float4(input.Position, 1.0f));
    float4 viewPosition = mul(view, worldPosition);
    output.Position = mul(proj, viewPosition);
    output.WorldNormal = normalize(mul((float3x3)pushConstants.model, input.Normal));
    output.UV = input.UV;
    output.HasTexcoord = pushConstants.meshInfo.x;
    return output;
}
