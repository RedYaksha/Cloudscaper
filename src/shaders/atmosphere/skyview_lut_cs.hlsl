#include "atmosphere_common.hlsl"
#include "common/render_common.hlsl"
#include "common/volumetric_rendering.hlsl"

struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};


#define SkyViewLUT_RootSignature \
"RootFlags(0), " \
"DescriptorTable( SRV(t0, numDescriptors = 2) )," \
"DescriptorTable( UAV(u0, numDescriptors = 1) )," \
"CBV(b0)," \
"CBV(b1), " \
"StaticSampler(s0," \
"addressU = TEXTURE_ADDRESS_CLAMP," \
"addressV = TEXTURE_ADDRESS_CLAMP," \
"addressW = TEXTURE_ADDRESS_CLAMP," \
"filter = FILTER_MIN_MAG_MIP_LINEAR)"

RWTexture2D<float4> skyviewLUT : register(u0);
Texture2D<float4> transmittanceLUT : register(t0);
Texture2D<float4> multiscatteringLUT : register(t1);

SamplerState lutSampler : register(s0);

ConstantBuffer<AtmosphereContext> atmosphere : register(b0);
ConstantBuffer<SkyBuffer> gSky : register(b1);

#define NUM_SKYVIEW_INTEGRATION_STEPS 30

float3 IntegrateLuminance(float3 rayOrigin, float3 rayDir, float3 lightDir) {
    const float numSamples = NUM_SKYVIEW_INTEGRATION_STEPS;
    
    const float distToAtmo = GetNearestRaySphereDistance(rayOrigin, rayDir, float3(0,0,0),  atmosphere.Rt);
    const float distToGround = GetNearestRaySphereDistance(rayOrigin, rayDir, float3(0,0,0),  atmosphere.Rb);
    const bool hitGround = distToGround != -1.0;

    // will be -1.0 if it didn't hit either
    const float sampleRayLength = hitGround? distToGround : distToAtmo;

    // remember, that it is assumed that light sources are directional lights
    // and thus we can pre-compute the Rayleigh and Mie phase functions
    const float cosTheta_dl = dot(rayDir, lightDir);

    // The phase functions angle, theta, is the angle between the "forward light direction",
    // which in this case is -1 * lightDir, and rayDir.
    //
    // This implies that we need to pass in cos(pi - theta) into these phase functions.
    // But using the trig identities we can deduce that:
    //      cos(pi - theta) = cos(pi)cos(theta) + sin(pi)sin(theta)
    //                      = (-1)cos(theta) + (0)sin(theta)
    //                      = -1 * cos(theta)
    // which is exactly what we use here.
    const float miePhase = MiePhaseApproximation_CornetteShanks(-1. * cosTheta_dl);

    // As the Rayleigh Phase function is symmetric, i.e. RayleighPhase(-cosTheta) = RayleighPhase(cosTheta),
    // negating cosTheta_dl here is optional
    const float rayleighPhase = RayleighPhase(-1. * cosTheta_dl);

    //
    const float tOffset = 0.3;
    
    // we have to gradually modify transmittance as we travel along the sample ray
    float3 transmittance = 1.0f;

    // the values to integrate
    float3 luminance = 0.0;
    float t = 0.0;

    // Essentially, for each iteration we will be calculating L_scat(rayOrigin, rayOrigin - t * rayDir, rayDir), see Eq. 3,
    // and "integrating" by adding it to the luminance variable, see Eq. 1.
    //
    // However, we can now incorporate the multi-scattering contribution psi_ms, so we use the new form of L_scat, see Eq. 11.
    for(float i = 0.0; i < numSamples; i += 1.0) {
        const float newT = sampleRayLength * (i + tOffset) / numSamples;
        const float dt = newT - t;
        t = newT;
        
        // From this point on, the "sample" refers to a segment of the ray such that
        //      - dt is the length of the segment
        //      - samplePos is at the end of the segment

        const float3 samplePos = rayOrigin + t * rayDir;
        
        // the following are used for sampling LUTs
        const float r = length(samplePos); // distance from the center of the planet
        const float3 zenith = samplePos / r; // zenith = "up" direction (away from center)
        
        // medium values/coefficients at the end of the sample segment
        const MediumSample sampleMedium = GetMediumSample(samplePos, atmosphere);

        // transmittance along the sample segment
        const float3 sampleTransmittance = exp(-1 * (sampleMedium.extinction * dt));

        // sigma_s(x - tv)
        const float3 scattering = sampleMedium.scattering;

        // T(x, x + t_{atmo} w_s) of S(x, w_s)
        // i.e. the transmittance from samplePos to the light source
        
        // the direction of interest v, is the lightDir
        const float cosTheta_Zl = dot(zenith, lightDir);
        const float3 lightTransmittance = GetLightTransmittance(transmittanceLUT, lutSampler, atmosphere, r, cosTheta_Zl);

        // p(v, li) from Eq. 3 is the sum of the Mie and Rayleigh phase contributions
        const float3 phaseTimesScattering = sampleMedium.scatteringMie * miePhase + sampleMedium.scatteringRayleigh * rayleighPhase;

        //
        const float3 psi_ms = GetMultiScattering(multiscatteringLUT, lutSampler, atmosphere, cosTheta_Zl, r);

        const float3 lightSourceL = gSky.sunIlluminance;

        //
        const float3 curLuminance = lightSourceL * (lightTransmittance * phaseTimesScattering + psi_ms * scattering); // lightSourceL * (lightTransmittance * phaseTimesScattering + psi_ms * scattering);

        
        // integrate the value, curLuminance, over segment, w.r.t. transmittance.
        // See this usage in multiscattering_lut_cs.hlsl for more info
        const float3 integralOverSegment_lum = (curLuminance - curLuminance * sampleTransmittance) / sampleMedium.extinction;

        // 
        luminance += integralOverSegment_lum * transmittance;
        //luminance += curLuminance * transmittance * dt;

        // update transmittance
        transmittance *= sampleTransmittance;
    }

    return luminance;
}



[RootSignature( SkyViewLUT_RootSignature )]
[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, THREAD_COUNT_Z)] 
void main( ComputeShaderInput IN ) {
    const uint3 Cell = IN.DispatchThreadID;

    // x,y mapped to [0,1]
    float TexWidth, TexHeight;
    skyviewLUT.GetDimensions(TexWidth, TexHeight);
    
    const float2 uv = float2((float)Cell.x,(float)Cell.y) / float2(TexWidth, TexHeight);

    // NOTE: cameraPos is above the ground, and is also a location where the ground is flat
    
    // worldPos is w.r.t. the origin as the center of the planet
    const float3 worldPos = gSky.cameraPos + float3(0, 0, atmosphere.Rb);
    
    const float r = length(worldPos);

    // cosine(angle between light and direction of interest, v)
    float cosTheta_lv;

    // cosine(angle between zenith and direction of interest, v)
    float cosTheta_Zv;

    UVToSkyViewParameters(atmosphere, uv, r, cosTheta_lv, cosTheta_Zv);


    // lightDir is a direction on the yz plane
    // To ensure cosTheta_lv makes sense, we transform lightDir to be on the xz plane
    float3 lightAlignedToXAxis;
    {
        const float3 upAxis = worldPos / r;
        const float cosTheta_Zl = dot(upAxis, normalize(gSky.lightDir));
        lightAlignedToXAxis = float3(sqrt(1 - cosTheta_Zl * cosTheta_Zl), 0, cosTheta_Zl);
    }
    
    const float sinTheta_Zv = sqrt(1 - cosTheta_Zv * cosTheta_Zv);
    const float sinTheta_lv = sqrt(1 - cosTheta_lv * cosTheta_lv);
    
    const float3 rayOrigin = float3(0, 0, r);
    const float3 rayDir = float3(sinTheta_Zv * cosTheta_lv, sinTheta_Zv * sinTheta_lv, cosTheta_Zv);

    const float3 L = IntegrateLuminance(rayOrigin, rayDir, lightAlignedToXAxis);

    skyviewLUT[Cell.xy] = float4(L, 1.0);
}
