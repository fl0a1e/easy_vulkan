struct VSInput
{
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float3 Color : COLOR;
};

struct VSOutput
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 Color : COLOR;
};

VSOutput main(VSInput input)
{
    VSOutput o;

    // 这里先用一个写死在 shader 里的简单透视投影，
    // 目的只是让顶点缓冲里的 3D 立方体坐标能在屏幕上形成近大远小的效果。
    float viewZ = input.Position.z + 2.5f;
    float scale = 0.9f / viewZ;

    o.Position = float4(input.Position.xy * scale, 0.5f, 1.0f);
    o.Color = input.Color;
    return o;
}