struct PSInput
{
    [[vk::location(0)]] float3 WorldNormal : NORMAL;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
    [[vk::location(2)]] nointerpolation float HasTexcoord : TEXCOORD1;
};

[[vk::binding(1, 0)]] Texture2D baseColorTexture;
[[vk::binding(2, 0)]] SamplerState baseColorSampler;

struct PSOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

PSOutput main(PSInput input)
{
    PSOutput output;
    float3 baseColor = input.HasTexcoord > 0.5f
        ? baseColorTexture.Sample(baseColorSampler, input.UV).rgb
        : float3(0.85f, 0.85f, 0.85f);

    output.Albedo = float4(baseColor, 1.0f);
    output.Normal = float4(normalize(input.WorldNormal), 1.0f);
    return output;
}
