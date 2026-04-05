struct PSInput
{
    [[vk::location(0)]] float3 Color : COLOR;
};

float4 main(PSInput input) : SV_TARGET0
{
    return float4(input.Color, 1.0f);
}