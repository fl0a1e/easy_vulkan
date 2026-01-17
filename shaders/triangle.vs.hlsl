struct VSOutput
{
    float4 Position : SV_Position;
};

VSOutput main(uint vertexIndex : SV_VertexID)
{
    float2 positions[3] =
    {
        float2(0.0f, -0.5f),
        float2(-0.5f, 0.5f),
        float2(0.5f, 0.5f)
    };

    VSOutput o;
    o.Position = float4(positions[vertexIndex], 0.0f, 1.0f);
    return o;
}
