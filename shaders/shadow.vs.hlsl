cbuffer ShadowData : register(b0)
{
    float4x4 lightViewProj;
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

float4 main(VSInput input) : SV_Position
{
    float4 worldPosition = mul(pushConstants.model, float4(input.Position, 1.0f));
    return mul(lightViewProj, worldPosition);
}
