struct PSInput
{
    [[vk::location(0)]] float3 WorldNormal : NORMAL;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
};

// C++ 侧的 descriptor set layout 对应：
// set 0 / binding 1 是 sampled image，binding 2 是 sampler。
[[vk::binding(1, 0)]] Texture2D baseColorTexture;
[[vk::binding(2, 0)]] SamplerState baseColorSampler;

// light
[[vk::binding(3, 0)]]
cbuffer LightData
{
    float3 light_position;
    float3 light_dir;
    float3 light_color;
}

float4 main(PSInput input) : SV_TARGET0
{
    float4 baseColor = baseColorTexture.Sample(baseColorSampler, input.UV);
    float3 normal = normalize(input.WorldNormal);
    float3 lightDirection = normalize(-light_dir);
    float diffuse = max(dot(normal, lightDirection), 0.0f);
    float3 litColor = baseColor.rgb * light_color.rgb * diffuse;
    return float4(litColor, baseColor.a);
}
