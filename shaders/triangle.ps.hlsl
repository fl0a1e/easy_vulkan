struct PSInput
{
    [[vk::location(0)]] float3 WorldNormal : NORMAL;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
    [[vk::location(2)]] nointerpolation float HasTexcoord : TEXCOORD1;
    [[vk::location(3)]] float3 WorldPosition : TEXCOORD2;
};

[[vk::binding(1, 0)]] Texture2D baseColorTexture;
[[vk::binding(2, 0)]] SamplerState baseColorSampler;

[[vk::binding(3, 0)]]
cbuffer LightData
{
    float3 light_position;
    float3 light_dir;
    float3 light_color;
}

cbuffer CameraData : register(b0)
{
    float4x4 view;
    float4x4 proj;
    float3 cameraPosition;
};

float4 main(PSInput input) : SV_TARGET0
{
    float3 baseColor = input.HasTexcoord > 0.5f
        ? baseColorTexture.Sample(baseColorSampler, input.UV).rgb
        : float3(0.85f, 0.85f, 0.85f);

    float3 normal = normalize(input.WorldNormal);
    float3 lightDirection = normalize(-light_dir);
    float3 viewDirection = normalize(cameraPosition - input.WorldPosition);
    float3 halfVector = normalize(lightDirection + viewDirection);

    float ambient = 0.12f;
    float diffuse = max(dot(normal, lightDirection), 0.0f);
    float specular = pow(max(dot(normal, halfVector), 0.0f), 32.0f);

    float3 diffuseColor = baseColor * light_color.rgb * diffuse;
    float3 ambientColor = baseColor * ambient;
    float3 specularColor = light_color.rgb * specular * 0.35f;
    return float4(ambientColor + diffuseColor + specularColor, 1.0f);
}
