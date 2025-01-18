struct PixelShaderInput
{
    float2 UVs : UV;
};

Texture2D<float4> ColorTex : register(t0);
SamplerState mySampler : register(s0);
SamplerState mySampler2 : register(s1);

float4 main( PixelShaderInput IN ) : SV_Target
{
    float4 color = ColorTex.Sample(mySampler2, IN.UVs);
    color += float4(1,0,1,1);
    return color;
    //return float4(IN.UVs, 0.0f, 1.0f);
}
