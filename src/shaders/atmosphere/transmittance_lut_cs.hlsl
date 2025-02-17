#include "atmosphere_common.hlsl"

struct ComputeShaderInput
{
    uint3 GroupID           : SV_GroupID;           // 3D index of the thread group in the dispatch.
    uint3 GroupThreadID     : SV_GroupThreadID;     // 3D index of local thread ID in a thread group.
    uint3 DispatchThreadID  : SV_DispatchThreadID;  // 3D index of global thread ID in the dispatch.
    uint  GroupIndex        : SV_GroupIndex;        // Flattened local index of the thread within a thread group.
};

#define TransmittanceLUT_RootSignature \
"RootFlags(0), " \
"DescriptorTable( UAV(u0, numDescriptors = 1) )," \
"CBV(b0), " \
"CBV(b1), " \

RWTexture2D<float4> transmittanceLUT : register(u0);
ConstantBuffer<AtmosphereContext> atmosphere : register(b0);
// ConstantBuffer<SkyBuffer> gSky : register(b1);

#define NUM_TRANSMITTANCE_INTEGRATION_STEPS 40

float3 IntegrateTransmittance(float3 startPos, float3 rayDir) {
    // NOTE: If the ray will hit the ground, then the transmittance
    // is not valid, as the parameterization is based off of the
    // ray endpoint, I, being the edge of the atmosphere (which would go through the ground).
    // We'll explicitly check for that when sampling the transmittanceLUT when we need its values.

    const float r = length(startPos);
    const float3 startZenith = startPos / r;
    rayDir = normalize(rayDir);
    
    // both are normalized, so dot = cos
    const float cosTheta_Zv = dot(startZenith, rayDir);
    
    const float sampleRayLength = DistanceToTopAtmosphere(atmosphere, r, cosTheta_Zv);
    const float numSamples = NUM_TRANSMITTANCE_INTEGRATION_STEPS;

    // length so far, along the ray
    float t = 0.0f;
    float3 transmittance = 1.f;
    float tOffset = 0.3f;
    
    for(float i = 0.0; i < numSamples; i += 1.0) {
        // Speculation: We can't use (i/numSamples) as a percentage along the ray
        // because to calculate transmittance, we need to calculate an integral along a line with positive length,
        // which would be violated when i=0 and we use t = sampleRayLength * (0 / numSamples)
        // Adding some positive number to i would ensure all samples have a dt > 0 where:
        //
        // Let numSamples = 40, and sampleRayLength = 100, tOffset = 0.3
        // i=0 => dt = 0.75, t = 0.75
        // i=1 => dt = 2.5, t = 3.25
        // i=2 => dt = 2.5, t = 5.75
        // i=3 => dt = 2.5, t = 8.25
        // ...
        // i=39 => dt = 2.5, t = 98.25
        //
        // actually no this doesn't make sense, why not just fix dt to (sampleRayLength/numSamples)?
        
        const float newT = sampleRayLength * (i + tOffset) / numSamples;
        const float dt = newT - t;
        t = newT;

        const float3 X = startPos + t * rayDir;
        
        const MediumSample medium = GetMediumSample(X, atmosphere);
        
        transmittance *= exp(-medium.extinction * dt);
    }

    return transmittance;
}

[RootSignature( TransmittanceLUT_RootSignature )]
[numthreads(THREAD_COUNT_X, THREAD_COUNT_Y, THREAD_COUNT_Z)] 
void main( ComputeShaderInput IN ) {
    // transmittanceLUT will store the transmittance of a ray starting at position X (with altitude r),
    // whose angle between the zenith direction is mu. The length of the ray depends on whether it
    // hits the edge of the atmosphere or the ground.
    //
    // When we want an answer to:
    // "What is the transmittance of a light ray from the sun to a point, X, in the atmosphere?"
    //
    // Then all we need to do is:
    // 1. Find the cosine(angle between sun direction and zenith) and the altitude of X, say r
    // 2. Parameterize it to (u,v) using the helper function LightTransmittanceParameteresToUV()
    // 3. Sample the transmittanceLUT, using the calculated (u,v).
    //
    // transmittanceLUT will be mainly used to find the transmittance of a light ray from the sun (using 1 sample).
    // But the LUT can be used to find the transmittance between any 2 points in the atmosphere (using 2 samples)!
    // This is because two points in the atmosphere, X1 and X2, can be represented by their altitudes, r1, and r2.
    // And the direction between X1 and X2, say j, correspond to a single cos(theta_Zv). One can then use the transmittanceLUT
    // to find T(X1, I) and T(X2, I), where I is the endpoint of the ray at direction j from each X.
    // And assuming X2 is closer to I than X1, by using properties of exponents one can deduce
    // that T(X1, X2) = T(X1, I) / T(X2, I)
    
    const uint3 Cell = IN.DispatchThreadID;

    // x,y,z mapped to [0,1]
    float TexWidth, TexHeight;
    transmittanceLUT.GetDimensions(TexWidth, TexHeight);
    
    const float2 uv = float2((float)Cell.x,(float)Cell.y) / float2(TexWidth, TexHeight);
    
    // radius of X (distance from the center of the planet)
    float r; 
    
    // mu = cos(theta_Zv) i.e. the cosine of the angle between the zenith direction and the direction of interest, v.
    float mu;
    
    // set r and mu
    UVToLightTransmittanceParameters(atmosphere, uv, r, mu);

    // It doesn't really matter what startPos is, just that the radius of
    // startPos is r. Setting it to be (0,0,r) is the simplest way to accomplish that.
    const float3 startPos = float3(0,0,r);
    
    // by setting rayDir.y = sin(theta_Zv), we're assuming the sun is rotating along the YZ plane 
    const float3 rayDir = float3(0, sqrt(1. - mu * mu), mu);

    // IntegrateTransmittance() will calculate the transmittance
    // from startPos to the point on the outer edge of the atmosphere in
    // the direction of rayDir.
    const float3 transmittance = IntegrateTransmittance(startPos, rayDir);
    
    // This texel will store the pre-integrated transmittance so
    // we can use it for later in just 1 sample.
    transmittanceLUT[Cell.xy] = isnan(transmittance).x? float4(1,1,1,1) : float4(transmittance, 1.0f);
}

