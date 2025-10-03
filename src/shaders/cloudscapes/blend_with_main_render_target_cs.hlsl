struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

#define BlendWithMainRenderTarget_RootSignature \
"RootFlags(0), " \
"DescriptorTable( UAV(u0, numDescriptors = 2) )," \

RWTexture2D<float4> srcRT : register(u0);
RWTexture2D<float4> dstRT : register(u1);

[RootSignature( BlendWithMainRenderTarget_RootSignature )]
[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, THREAD_COUNT_Z)] 
void main( ComputeShaderInput IN ) {
    
    // writes to RWTexture3D => XY spans single depth slice (where [0][0] is top left), Z is depth slice index
    const uint3 cell = IN.DispatchThreadID;

    uint curWidth, curHeight;
    dstRT.GetDimensions(curWidth, curHeight);

    if(cell.x >= curWidth || cell.y >= curHeight) {
        return;
    }

    // calc which is first
    uint2 pixelPos = cell.xy;

    const float4 srcVal = srcRT[pixelPos];
    const float3 dstColor = dstRT[pixelPos].rgb;
    const float3 finalColor = dstColor * srcVal.a + srcVal.rgb * (1 - srcVal.a);
    
    dstRT[pixelPos] = float4(finalColor, 1.0f);
}
