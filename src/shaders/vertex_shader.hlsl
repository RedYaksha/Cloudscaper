struct VertexPosColor
{
    float4 Position : POSITION;
    float2 UVs : UV;
};
 
struct VertexShaderOutput
{
    float2 UVs : UV;
    float4 Position : SV_Position;
};

#define Test_RootSignature \
"RootFlags(0), " \
"DescriptorTable( SRV(t1, numDescriptors = 2) )," \
"RootConstants(num32BitConstants=1, b0),"\
"DescriptorTable( Sampler(s0, numDescriptors = 1) )," \

//"StaticSampler(s0," \
//"addressU = TEXTURE_ADDRESS_CLAMP," \
//"addressV = TEXTURE_ADDRESS_CLAMP," \
//"addressW = TEXTURE_ADDRESS_CLAMP," \
//"filter = FILTER_MIN_MAG_MIP_LINEAR)"

//Texture2D<float4> tex1 : register(t1);
//Texture2D<float4> tex2 : register(t2);
//SamplerState s : register(s0);
float globalVal : register(b0);

// [RootSignature( Test_RootSignature )]
VertexShaderOutput main(VertexPosColor IN)
{
    VertexShaderOutput OUT;
 
    //float4 c = globalVal * tex1.SampleLevel(s, float2(0,0), 0);
    //float4 d = tex2.SampleLevel(s, float2(0,0), 0);
    
    OUT.Position = IN.Position + float4(globalVal, 0, 0, 0);
    OUT.UVs = IN.UVs;

 
    return OUT;
}
