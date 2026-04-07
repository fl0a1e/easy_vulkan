cbuffer CameraData : register(b0)
{
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float3 Color : COLOR;
    [[vk::location(2)]] float2 UV : TEXCOORD0;
};

struct VSOutput
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 Color : COLOR;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    // 顶点缓冲里的局部空间坐标，会依次经过 model / view / proj 变换，
    // 最后写进 SV_Position，交给后面的裁剪和光栅化阶段使用。
    float4 localPosition = float4(input.Position, 1.0f);
    float4 worldPosition = mul(model, localPosition);
    float4 viewPosition = mul(view, worldPosition);
    o.Position = mul(proj, viewPosition);

    // 颜色和 UV 都作为普通 varying 输出到下一个阶段，由光栅化阶段自动插值。
    o.Color = input.Color;
    o.UV = input.UV;
    return o;
}
