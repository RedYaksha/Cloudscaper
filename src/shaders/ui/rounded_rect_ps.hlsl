struct PixelShaderInput 
{
    float4 color : COLOR;
    float4 radii : RADII;
    float4 transform : TRANSFORM;
    float4 position : SV_Position;
};

struct GlobalData {
    float2 screenSize;
};

ConstantBuffer<GlobalData> global : register(b0);

// b.x = half width
// b.y = half height
// r.x = roundness top-right  
// r.y = roundness bottom-right
// r.z = roundness top-left
// r.w = roundness bottom-left
//
// https://www.shadertoy.com/view/4llXD7
float sdRoundedBox(float2 p, float2 b, float4 r )
{
    r.xy = (p.x>0.0)?r.xy : r.zw;
    r.x  = (p.y>0.0)?r.x  : r.y;
    float2 q = abs(p)-b+r.x;
    return min(max(q.x,q.y),0.0) + length(max(q,0.0)) - r.x;
}

float sdCircle( float2 p, float r )
{
    return length(p) - r;
}

float4 main(PixelShaderInput input) : SV_Target
{
    const float2 screenPosBase = input.transform.xy;
    const float2 screenSize = input.transform.zw;
    const float2 p = input.position.xy - screenPosBase - screenSize / 2;

    // signed dist
    const float sd = sdRoundedBox(p, input.transform.zw / 2.f, input.radii);

    // percentage of signed distance between X and 0
    float falloff = 0.22;
    float pc = abs(falloff - sd) / falloff;
    const float alpha = sd < 0? input.color.a : lerp(1, 0, pc);
    
    return float4(input.color.rgb, alpha);
}
