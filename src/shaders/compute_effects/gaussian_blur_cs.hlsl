struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

#define GaussianBlur_RootSignature \
"RootFlags(0), " \
"DescriptorTable( SRV(t0, numDescriptors = 1) )," \
"DescriptorTable( UAV(u0, numDescriptors = 1) )," \
"RootConstants(num32BitConstants=1, b0)," \
"StaticSampler(s0," \
"addressU = TEXTURE_ADDRESS_CLAMP," \
"addressV = TEXTURE_ADDRESS_CLAMP," \
"addressW = TEXTURE_ADDRESS_CLAMP," \
"filter = FILTER_MIN_MAG_MIP_LINEAR)"

Texture2D<float4> srcRT : register(t0);
RWTexture2D<float4> dstRT : register(u0);
SamplerState srcSampler : register(s0);
float blurRadius : register(b0);

[RootSignature( GaussianBlur_RootSignature )]
[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, THREAD_COUNT_Z)] 
void main( ComputeShaderInput IN ) {
    const float kernel[9] = {
        .0625, .125, .0625,
        .125,  .25,  .125,
        .0625, .125, .0625
    };
    
    // writes to RWTexture3D => XY spans single depth slice (where [0][0] is top left), Z is depth slice index
    const uint3 cell = IN.DispatchThreadID;

    uint curWidth, curHeight;
    dstRT.GetDimensions(curWidth, curHeight);

    if(cell.x >= curWidth || cell.y >= curHeight) {
        return;
    }

    // calc which is first
    uint2 pixelPos = cell.xy;
    float2 uv = pixelPos / float2(curWidth, curHeight);

    float2 txOffset = blurRadius / float2(curWidth, curHeight);
    float2 offsets[9] = { 
         float2(-txOffset.x,  txOffset.y),  // top-left
         float2( 			0.,   txOffset.y),  // top-center
         float2( txOffset.x,  txOffset.y),  // top-right
         float2(-txOffset.x,  			 0.),  // center-left
         float2( 			0.,			 	 0.),  // center-center
         float2( txOffset.x,  	 		 0.),  // center-right
         float2(-txOffset.x,  -txOffset.y), // bottom-left
         float2( 			0.,   -txOffset.y), // bottom-center
         float2( txOffset.x,  -txOffset.y)  // bottom-right    
    }; 

    float4 outColor= 0.;
    
    for(int i = 0; i < 9; i++) {
        outColor += kernel[i] * srcRT.SampleLevel(srcSampler, uv + offsets[i], 0);
    }

    dstRT[pixelPos] = outColor;
}
