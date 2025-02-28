struct VertexInput 
{
    // per vertex
    float4 position : POSITION;
    float2 uv: UV;

    // per instance
    float4 color : COLOR;
    float4 transform : TRANSFORM; // (tX, tY, sX, sY)
    float4 clipTransform: CLIP_TRANSFORM; // (tX, tY, sX, sY)
    float2 uvStart : UV_START;
    float2 uvEnd : UV_END;
};
 
struct VertexShaderOutput
{
    float4 color : COLOR;
    float4 clipTransform: CLIP_TRANSFORM; // (tX, tY, sX, sY)
    float2 uv : UV;
    float4 position : SV_Position;
};

struct GlobalData {
    float2 screenSize;
};

ConstantBuffer<GlobalData> global : register(b0);

VertexShaderOutput main(VertexInput input)
{
    const float2 posPx = input.transform.xy;
    const float2 sizePx = input.transform.zw;
    
    const float2 vertPos = input.position.xy * float2(1, 1); // invert Y

    // 
    const float2 screenPosAlpha = (posPx / global.screenSize) + vertPos * (sizePx / global.screenSize); // in range [0, 1]
    const float2 ndc = float2(-1, 1) + (screenPosAlpha * float2(1, -1) * 2.0f);

    float2 uv;
    uv.x = input.uvStart.x + (input.uvEnd.x - input.uvStart.x) * input.uv.x;
    uv.y = input.uvStart.y + (input.uvEnd.y - input.uvStart.y) * input.uv.y;
    
    VertexShaderOutput ret;
    ret.position = float4(ndc, 0.f, 1.f);
    ret.clipTransform = input.clipTransform;
    ret.color = input.color;
    ret.uv = uv;
    
    return ret;
}
