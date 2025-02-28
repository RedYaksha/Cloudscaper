struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

Texture2D<float4> colorTex : register(t0);
RWTexture2D<float4> outTex : register(u0);

// TODO: num threads should be macros

[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, THREAD_COUNT_Z)] 
void main( ComputeShaderInput IN )
{
    uint3 cell = IN.DispatchThreadID;

    uint srcWidth, srcHeight;
    colorTex.GetDimensions(srcWidth, srcHeight);

    if(cell.x >= srcWidth || cell.y >= srcHeight) {
        return;
    }
    
    outTex[cell.xy] = colorTex[cell.xy]; // * float4(1.5, 0.3, 0.3, 1);
}
