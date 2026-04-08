cbuffer CameraData : register(b0)
{
    float4x4 view;
    float4x4 proj;
};

struct ModelData
{
    float4x4 model;
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
};

VSOutput main(VSInput input)
{
    VSOutput o;

    float4 localPosition = float4(input.Position, 1.0f);
    float4 worldPosition = mul(pushConstants.model, localPosition);
    float4 viewPosition = mul(view, worldPosition);
    o.Position = mul(proj, viewPosition);

    o.WorldNormal = normalize(mul((float3x3)pushConstants.model, input.Normal));
    o.UV = input.UV;
    return o;
}
