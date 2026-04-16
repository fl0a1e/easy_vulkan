struct PSInput
{
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

[[vk::binding(0, 0)]] Texture2D sceneColorTexture;
[[vk::binding(1, 0)]] SamplerState sceneColorSampler;

float4 main(PSInput input) : SV_Target0
{
    float3 color = sceneColorTexture.Sample(sceneColorSampler, saturate(input.UV)).rgb;

    // 第一版后处理先走最小效果：轻量 tone mapping 后转灰度，
    // 目的是把 scene color -> fullscreen postprocess -> swapchain 链路跑通。
    float3 mapped = color / (1.0f + color);
    float luminance = dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
    return float4(luminance.xxx, 1.0f);
}
