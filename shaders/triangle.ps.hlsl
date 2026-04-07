struct PSInput
{
    [[vk::location(0)]] float3 Color : COLOR;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
};

// C++ 侧的 descriptor set layout 对应：
// set 0 / binding 1 是 sampled image，binding 2 是 sampler。
[[vk::binding(1, 0)]] Texture2D baseColorTexture;
[[vk::binding(2, 0)]] SamplerState baseColorSampler;

float4 main(PSInput input) : SV_TARGET0
{
    float4 baseColor = baseColorTexture.Sample(baseColorSampler, input.UV);
    return float4(baseColor.rgb * input.Color, baseColor.a);
}
