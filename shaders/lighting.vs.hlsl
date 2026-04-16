struct VSOutput
{
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 UV : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };

    const float2 uvs[3] = {
        float2(0.0f, 0.0f),
        float2(0.0f, 2.0f),
        float2(2.0f, 0.0f)
    };

    VSOutput output;
    output.Position = float4(positions[vertexId], 0.0f, 1.0f);
    output.UV = uvs[vertexId];
    return output;
}
