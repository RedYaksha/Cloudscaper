struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

#define TAA_RootSignature \
"RootFlags(0), " \
"DescriptorTable( UAV(u0, numDescriptors = 2) )," \
"RootConstants(num32BitConstants=1, b0)," \

RWTexture2D<float4> rt0 : register(u0);
RWTexture2D<float4> rt1 : register(u1);

struct TAAInfo {
    int curInd; // index of current render target
    //float3 pad0;
};

ConstantBuffer<TAAInfo> taaInfo : register(b0);

[RootSignature( TAA_RootSignature )]
[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, THREAD_COUNT_Z)] 
void main( ComputeShaderInput IN ) {
    // writes to RWTexture3D => XY spans single depth slice (where [0][0] is top left), Z is depth slice index
    const uint3 cell = IN.DispatchThreadID;

    uint curWidth, curHeight;
    rt0.GetDimensions(curWidth, curHeight);

    if(cell.x >= curWidth || cell.y >= curHeight) {
        return;
    }

    // calc which is first
    uint2 pixelPos = cell.xy;
    float4 val0 = rt0[pixelPos];
    float4 val1 = rt1[pixelPos];
    
    float4 oldVal = taaInfo.curInd == 0? val1 : val0;
    float4 curVal = taaInfo.curInd == 0? val0 : val1;

    // TODO: Reprojection

    // Color Clamping
    float3 minColor = 99999.;
    float3 maxColor = -99999.;
    
    for(int x = -1; x <= 1; x++) {
        for(int y = -1; y <= 1; y++) {
            const float3 c = rt0[pixelPos + float2(x,y)].rgb;
            minColor = min(minColor, c);
            maxColor = max(maxColor, c);
        }
    }

    float4 clampedOldVal = float4(clamp(oldVal.rgb, minColor, maxColor).rgb, oldVal.a);

    const float pOld = 0.9;
    const float4 newVal = pOld * clampedOldVal + (1 - pOld) * curVal;
    //
    if(taaInfo.curInd == 0) {
        rt0[pixelPos] = newVal;
    }
    else {
        rt1[pixelPos] = newVal;
    }
}
