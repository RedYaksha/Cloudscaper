struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

#define Texture3DMipMaps_RootSignature \
"RootFlags(0), " \
"DescriptorTable( UAV(u0, numDescriptors = 2) )," \

RWTexture3D<float4> sourceTexture : register(u0);
RWTexture3D<float4> destinationTexture : register(u1);

void resolveCell(uint3 base, bool3 addCell, bool3 actionMask, out uint3 outCell) {
    uint3 ret = base;
    if(actionMask.x) {
        if(addCell.x) {
            ret.x += 1;
        }
        else {
            ret.x -= 1;
        }
    }
    
    if(actionMask.y) {
        if(addCell.y) {
            ret.y += 1;
        }
        else {
            ret.y -= 1;
        }
    }
    
    if(actionMask.z) {
        if(addCell.z) {
            ret.z += 1;
        }
        else {
            ret.z -= 1;
        }
    }

    outCell = ret;
}

[RootSignature( Texture3DMipMaps_RootSignature )]
[numthreads(32, 32, 1)] // depends on destination texture size...right? or we can just discard extra
void main( ComputeShaderInput IN ) {
    // writes to RWTexture3D => XY spans single depth slice (where [0][0] is top left), Z is depth slice index
    const uint3 Cell = IN.DispatchThreadID;

    uint srcWidth, srcHeight, srcDepth;
    sourceTexture.GetDimensions(srcWidth, srcHeight, srcDepth);
    
    uint dstWidth, dstHeight, dstDepth;
    destinationTexture.GetDimensions(dstWidth, dstHeight, dstDepth);

    if(Cell.x >= dstWidth || Cell.y >= dstHeight || Cell.z >= dstDepth) {
        return;
    }
    
    // x,y,z mapped to [0,1]
    const float3 p = float3((float)Cell.x,(float)Cell.y,(float)Cell.z) * (1.f/(float)dstWidth);

    const uint startInd = 1;
    const uint endInd = srcWidth - 2;

    const uint3 dstIndOnSrc = startInd + p * (endInd - startInd);

    // depending on which octant we're in, choose correct indices to take average of
    const bool3 addCell = dstIndOnSrc < dstWidth / 2.;
    const int3 deltaCell = int3(addCell.x? 1 : -1, addCell.y? 1 : -1, addCell.z? 1 : -1);

    uint3 cells[8] = {uint3(0,0,0),uint3(0,0,0),uint3(0,0,0),uint3(0,0,0),uint3(0,0,0),uint3(0,0,0),uint3(0,0,0),uint3(0,0,0)};
    resolveCell(dstIndOnSrc, addCell, bool3(false, false, false), cells[0]);
    resolveCell(dstIndOnSrc, addCell, bool3(false, true, false), cells[1]);
    resolveCell(dstIndOnSrc, addCell, bool3(false, false, true), cells[2]);
    resolveCell(dstIndOnSrc, addCell, bool3(false, true, true), cells[3]);
    resolveCell(dstIndOnSrc, addCell, bool3(true, false, false), cells[4]);
    resolveCell(dstIndOnSrc, addCell, bool3(true, true, false), cells[5]);
    resolveCell(dstIndOnSrc, addCell, bool3(true, false, true), cells[6]);
    resolveCell(dstIndOnSrc, addCell, bool3(true, true, true), cells[7]);

    float4 value = 0.;
    for(int i = 0; i < 8; i++) {
        value += sourceTexture[cells[i]];
    }
    value /= 8.;

    // assume source and destination are a cube
    destinationTexture[Cell] = value; //float4(1,0,0,1);
}
