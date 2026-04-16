struct BlurPushConstants
{
    int4 direction;
};

[[vk::push_constant]]
BlurPushConstants pushConstants;

[[vk::binding(0, 0)]] Texture2D<float4> inputImage;
[[vk::binding(1, 0)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> outputImage;

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0;
    uint height = 0;
    inputImage.GetDimensions(width, height);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height)
        return;

    int2 pixel = int2(dispatchThreadId.xy);
    int2 direction = pushConstants.direction.xy;
    int2 maxCoord = int2(int(width) - 1, int(height) - 1);

    // Fixed 5-tap Gaussian-like kernel; two dispatches make it separable.
    static const float weights[3] = {
        0.22702703f,
        0.19459459f,
        0.12162162f
    };

    float4 color = inputImage.Load(int3(pixel, 0)) * weights[0];
    // [unroll] 是“提示编译器展开循环”，不是“不展开”
    [unroll] 
    for (int offset = 1; offset <= 2; ++offset) {
        int2 delta = direction * offset;
        int2 positiveCoord = clamp(pixel + delta, int2(0, 0), maxCoord);
        int2 negativeCoord = clamp(pixel - delta, int2(0, 0), maxCoord);
        color += inputImage.Load(int3(positiveCoord, 0)) * weights[offset];
        color += inputImage.Load(int3(negativeCoord, 0)) * weights[offset];
    }

    outputImage[pixel] = color;
}
