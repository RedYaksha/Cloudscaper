struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

#define ComputeModelNoise_RootSignature \
"RootFlags(0), " \
"DescriptorTable( UAV(u0, numDescriptors = 1) )," \

RWTexture3D<float4> NoiseTexture : register(u0);

// noise functions
uint MurmurHash3D(float3 x, uint seed) {
    const uint m = 0x5bd1e995U;
    uint hash = seed;

    uint k = (uint) x.x;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    
    k = (uint) x.y;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;
    
    k = (uint) x.z;
    k *= m;
    k ^= k >> 24;
    k *= m;
    hash *= m;
    hash ^= k;

    hash ^= hash >> 13;
    hash *= m;
    hash ^= hash >> 15;

    return hash;
}

float3 GradientDirection(uint hash) {
    switch (int(hash) & 15) {
    // look at the last four bits to pick a gradient direction
        case 0:
            return float3(1, 1, 0);
    case 1:
        return float3(-1, 1, 0);
    case 2:
        return float3(1, -1, 0);
    case 3:
        return float3(-1, -1, 0);
    case 4:
        return float3(1, 0, 1);
    case 5:
        return float3(-1, 0, 1);
    case 6:
        return float3(1, 0, -1);
    case 7:
        return float3(-1, 0, -1);
    case 8:
        return float3(0, 1, 1);
    case 9:
        return float3(0, -1, 1);
    case 10:
        return float3(0, 1, -1);
    case 11:
        return float3(0, -1, -1);
    case 12:
        return float3(1, 1, 0);
    case 13:
        return float3(-1, 1, 0);
    case 14:
        return float3(0, -1, 1);
    case 15:
        return float3(0, -1, -1);
    default:
        return float3(0,0,0);
    }
}

float Perlin(float3 p) {
    const float3 int_part = floor(p);
    const float3 fract_part = frac(p);
        
    uint seed = 0x578437adU;
        
    // generate cube grid points
    const float3 g0 = int_part; // bl
    const float3 g1 = int_part + float3(1.f, 0.f, 0.f); // br
    const float3 g2 = int_part + float3(0.f, 1.f, 0.f); // tl 
    const float3 g3 = int_part + float3(1.f, 1.f, 0.f); // tr 
    
    const float3 g4 = int_part + float3(0.f, 0.f, 1.f); // br
    const float3 g5 = int_part + float3(1.f, 0.f, 1.f); // tl 
    const float3 g6 = int_part + float3(0.f, 1.f, 1.f); // tr 
    const float3 g7 = int_part + float3(1.f, 1.f, 1.f); // tr

    // generate dot product between grid point unit vectors and (p - grid point)
    const float d0 = dot(GradientDirection(MurmurHash3D(g0, seed)), p - g0);
    const float d1 = dot(GradientDirection(MurmurHash3D(g1, seed)), p - g1);
    const float d2 = dot(GradientDirection(MurmurHash3D(g2, seed)), p - g2);
    const float d3 = dot(GradientDirection(MurmurHash3D(g3, seed)), p - g3);
    const float d4 = dot(GradientDirection(MurmurHash3D(g4, seed)), p - g4);
    const float d5 = dot(GradientDirection(MurmurHash3D(g5, seed)), p - g5);
    const float d6 = dot(GradientDirection(MurmurHash3D(g6, seed)), p - g6);
    const float d7 = dot(GradientDirection(MurmurHash3D(g7, seed)), p - g7);

    // use fract part of position to interpolate between the dot products.
    // the fract part is transformed by a smoothing function first
    //
    // smoothing function
    //      6t^5 - 15t^4 + 10t^3
    const float3 t = fract_part;
    const float3 u = t * t * t * ( t * (t * 6.0f - 15.f) + 10.f);
    
    // mix
    const float M0 = lerp(d0, d1, u.x);
    const float M1 = lerp(d2, d3, u.x);
    const float M2 = lerp(d4, d5, u.x);
    const float M4 = lerp(d6, d7, u.x);

    const float M5 = lerp(M0, M1, u.y);
    const float M6 = lerp(M2, M4, u.y);

    const float M7 = lerp(M5, M6, u.z);

    return M7;
}

float3 Hash33(float3 p) {
    float3 P = frac(p * float3(0.1031f, 0.11369f, 0.13787f));
    float3 P_yxz = {p.y, p.x, p.z};
    P = P + dot(P, P_yxz + 19.19f);
    return -1.0f + 2.0f *
        frac(float3(
            (P.x + P.y)*P.z,
            (P.x + P.z)*P.y,
            (P.y + P.z)*P.x
        ));
}

inline float Worley(float3 p, float scale) {
    // based on "repeatable 3d worley noise" by hong1991
    // https://www.shadertoy.com/view/3d3fWN
    
    float3 grid_point = floor(p * scale); // center grid point
    float3 fract_part = frac(p * scale);

    float min_dist = 1000000.f; // std::numeric_limits<float>::max();

    //
    for(int x = -1; x <= 1; x++) {
        for(int y = -1; y <= 1; y++) {
            for(int z = -1; z <= 1; z++) {
                float3 offset = float3((float)x,(float)y,(float)z);
                float3 cur_grid_point = grid_point + offset;
                float3 rId = Hash33(fmod(cur_grid_point, scale)) * 0.5f + 0.5f;

                // float3 r = (grid_point + fract_part) - (grid_point + offset) - rId;
                float3 r = offset + rId - fract_part;

                float d = dot(r, r);

                if(d < min_dist) {
                    min_dist = d;
                }
            }
        }
    }

    return min_dist;
}

float PerlinFBM(float3 p) {
    float gain = 0.5f;
    float lacunarity = 2.f;
    int octaves = 3;

    float amplitude = 0.5;
    float freq = 8.f;
    
    float ampSum = 0.f;

    float val = 0.f;
    for(int i = 0; i < octaves; i++) {
        val += amplitude * Perlin(p * freq);
        freq *= lacunarity;
        
        ampSum += amplitude;
        amplitude *= amplitude;
    }
    return clamp((val / ampSum) * 0.5f + 0.5f, 0.0f, 1.0f);
}

float Remap(float val, float original_min, float original_max, float new_min, float new_max) {
    float p = (val - original_min) / (original_max - original_min);
    return new_min + p * (new_max - new_min);
}

// ~end noise functions

[RootSignature( ComputeModelNoise_RootSignature )]
[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, THREAD_COUNT_Z)] 
void main( ComputeShaderInput IN ) {
    // writes to RWTexture3D => XY spans single depth slice (where [0][0] is top left), Z is depth slice index
    const uint3 Cell = IN.DispatchThreadID;

    // x,y,z mapped to [0,1]
    const float3 coord = float3((float)Cell.x,(float)Cell.y,(float)Cell.z) * (1.f/128.f);

    const float cell_count = 4.f;
    
    const float worley0 = (1.f - Worley(coord, cell_count * 1.f));
    const float worley1 = (1.f - Worley(coord, cell_count * 2.f));
    const float worley2 = (1.f - Worley(coord, cell_count * 4.f));
    const float worley3 = (1.f - Worley(coord, cell_count * 8.f));
    const float worley4 = (1.f - Worley(coord, cell_count * 16.f));

    const float worley_fbm0 = worley1 * 0.625f + worley2 * 0.25f + worley3 * 0.125f;
    const float worley_fbm1 = worley2 * 0.625f + worley3 * 0.25f + worley4 * 0.125f;
    const float worley_fbm2 = worley3 * 0.75f + worley4 * 0.25f;
    
    float4 Output = float4(worley_fbm0, worley_fbm1, worley_fbm2, 0.0f);
    // float4 c = float4(Cell.z / 128.f,0,0,1);
    NoiseTexture[Cell] = Output;
}
