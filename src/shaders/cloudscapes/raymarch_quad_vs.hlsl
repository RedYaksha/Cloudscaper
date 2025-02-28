struct VSIn {
    float4 Pos : POSITION;
    float2 UV : UV;
};

struct VSOut {
    float2 UV : UV;
    float4 Pos : SV_Position;
};


VSOut main(VSIn In) {
    VSOut Out;
    Out.UV = In.UV;

    // the vertices are a unit square (sides = [-0.5, 0.5],
    // but to ensure the quad fills the screen we multiply the vertices position by 2 so the sides are [-1,1]
    Out.Pos = float4(2 * In.Pos.xyz, 1);

    
    
    return Out;
}