#ifndef GAME_COMMON_RAY_MARCHING_COMMON_HLSL_
#define GAME_COMMON_RAY_MARCHING_COMMON_HLSL_

struct RenderContext
{
    float4x4 invProjectionMat;
    float4x4 invViewMat;
    uint2 screenSize;
    uint frame;
    float pad0;
    float3 cameraPos;
    float time; // total elapsed time
};

// Source: https://en.wikipedia.org/wiki/SRGB#The_forward_transformation_(CIE_XYZ_to_sRGB)
float3 ConvertToSRGB( float3 x )
{
    return x < 0.0031308 ? 12.92 * x : 1.055 * pow(abs(x), 1.0 / 2.4) - 0.055;
}

#endif // GAME_COMMON_RAY_MARCHING_COMMON_HLSL_
