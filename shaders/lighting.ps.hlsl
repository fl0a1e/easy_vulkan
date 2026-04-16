struct PSInput
{
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

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

[[vk::binding(1, 0)]]
cbuffer LightData
{
    float3 lightPosition;
    float _pad1;
    float3 lightDir;
    float _pad2;
    float3 lightColor;
    float _pad3;
};

[[vk::binding(2, 0)]]
cbuffer ShadowData
{
    float4x4 lightViewProj;
};

[[vk::binding(3, 0)]] Texture2D gAlbedoTexture;
[[vk::binding(4, 0)]] SamplerState gBufferSampler;
[[vk::binding(5, 0)]] Texture2D gNormalTexture;
[[vk::binding(6, 0)]] Texture2D<float> gDepthTexture;
[[vk::binding(7, 0)]] SamplerState depthSampler;
[[vk::binding(8, 0)]] Texture2D<float> shadowMapTexture;

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    // Projection() 已经在 CPU 侧做了 Vulkan 所需的 Y flip，
    // 这里重建 clip/NDC 位置时不能再额外翻一次。
    float2 ndcXY = float2(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f);
    float4 clipPosition = float4(ndcXY, depth, 1.0f);
    float4 viewPosition = mul(projInv, clipPosition);
    viewPosition /= viewPosition.w;
    return mul(viewInv, float4(viewPosition.xyz, 1.0f)).xyz;
}

float ComputeShadowFactor(float3 worldPosition)
{
    float4 shadowPosition = mul(lightViewProj, float4(worldPosition, 1.0f));
    if (shadowPosition.w <= 0.0f)
        return 1.0f;

    float3 projected = shadowPosition.xyz / shadowPosition.w;
    if (projected.x < -1.0f || projected.x > 1.0f ||
        projected.y < -1.0f || projected.y > 1.0f ||
        projected.z <= 0.0f || projected.z > 1.0f)
        return 1.0f;

    float2 shadowUv = projected.xy * 0.5f + 0.5f;
    float currentDepth = projected.z;
    float shadowDepth = shadowMapTexture.Sample(depthSampler, shadowUv).r;
    float bias = 0.0025f;
    return currentDepth - bias <= shadowDepth ? 1.0f : 0.28f;
}

float4 main(PSInput input) : SV_Target0
{
    float2 uv = saturate(input.UV);
    float depth = gDepthTexture.Sample(depthSampler, uv).r;
    if (depth >= 0.99999f)
        return float4(0.04f, 0.04f, 0.05f, 1.0f);

    float3 baseColor = gAlbedoTexture.Sample(gBufferSampler, uv).rgb;
    float3 normal = normalize(gNormalTexture.Sample(gBufferSampler, uv).xyz);
    float3 worldPosition = ReconstructWorldPosition(uv, depth);

    float3 lightDirection = normalize(-lightDir);
    float3 viewDirection = normalize(cameraPosition - worldPosition);
    float3 halfVector = normalize(lightDirection + viewDirection);
    float shadowFactor = ComputeShadowFactor(worldPosition);

    float ambient = 0.12f;
    float diffuse = max(dot(normal, lightDirection), 0.0f);
    float specular = pow(max(dot(normal, halfVector), 0.0f), 32.0f);

    float3 ambientColor = baseColor * ambient;
    float3 diffuseColor = baseColor * lightColor * diffuse * shadowFactor;
    float3 specularColor = lightColor * specular * 0.35f * shadowFactor;
    return float4(ambientColor + diffuseColor + specularColor, 1.0f);
}
