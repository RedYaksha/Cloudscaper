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

#define MultiScatteringLUT_RootSignature \
"RootFlags(0), " \
"DescriptorTable( SRV(t0, numDescriptors = 1) )," \
"DescriptorTable( UAV(u0, numDescriptors = 1) )," \
"CBV(b0)," \
"CBV(b1), " \
"StaticSampler(s0," \
"addressU = TEXTURE_ADDRESS_CLAMP," \
"addressV = TEXTURE_ADDRESS_CLAMP," \
"addressW = TEXTURE_ADDRESS_CLAMP," \
"filter = FILTER_MIN_MAG_MIP_LINEAR)"

RWTexture2D<float4> multiscatteringLUT : register(u0);
Texture2D<float4> transmittanceLUT : register(t0);
ConstantBuffer<AtmosphereContext> atmosphere : register(b0);

SamplerState lutSampler : register(s0);

// sample counts over a sphere for each point
#define SAMPLECOUNT 64
#define SQRTSAMPLECOUNT 8
#define NUM_MULTISCATTER_INTEGRATION_STEPS 20

//
//
void IntegrateLuminanceAndEnergyTransfer(float3 rayOrigin, float rayDir, out float3 integratedLuminance, out float3 integratedEnergyTransfer) {
    
    const float numSamples = NUM_MULTISCATTER_INTEGRATION_STEPS;
    
    // the ray may hit the edge of the atmosphere or the ground, or neither
    //
    // Since this is an arbitrary ray, whose origin may/may not be outside the atmosphere,
    // and may/may not hit the atmosphere - we cannot use Bruneton's DistanceToTopAtmosphere() nor
    // DistanceToBottomAtmosphere() to give us the sample length of the ray
    //
    const float distToAtmo = GetNearestRaySphereDistance(rayOrigin, rayDir, float3(0,0,0),  atmosphere.Rt);
    const float distToGround = GetNearestRaySphereDistance(rayOrigin, rayDir, float3(0,0,0),  atmosphere.Rb);
    const bool hitGround = distToGround != -1.0;

    // will be -1.0 if it didn't hit either
    float sampleRayLength = hitGround? distToGround : distToAtmo;

    // constants throughout the integration
    const float tOffset = 0.3;
    
    // isotropicPhase = pu = 1/4pi (see section 4 in Hillaire's paper)
    const float isotropicPhase = 1.0 / (4.0 * PI);
    
    // the values we're integrating
    float3 luminance = 0.0;
    float3 energyTransfer = 0.0;

    // we have to gradually modify transmittance to represent T(x, x-tv) in Eqn. 6
    float3 transmittance = 1.0f;

    // this is the t (distance along the ray) in Eqn. 6
    float t = 0.0;

    for(float i = 0; i < numSamples; i += 1.0) {
        const float newT = sampleRayLength * (i + tOffset) / numSamples;
        const float dt = newT - t;
        t = newT;

        // From this point on, the "sample" refers to a segment of the ray such that
        //      - dt is the length of the segment
        //      - samplePos is at the end of the segment 

        // position at the end of the segment
        const float3 samplePos = rayOrigin + t * rayDir;
        // distance from the center, used for sampling LUTs
        const float r = length(samplePos);
        const float3 zenith = samplePos / r;

        // medium values/coefficients at the end of the sample segment
        const MediumSample sampleMedium = GetMediumSample(samplePos, atmosphere);

        // transmittance along the sample segment
        const float3 sampleTransmittance = exp(-1 * (sampleMedium.extinction * dt));

        // sigma_s(x - tv) from Eqn. 6
        const float3 scattering = sampleMedium.scattering;

        // T(x, x + t_{atmo} w_s) of S(x, w_s)
        // i.e. the transmittance from samplePos to the light source 
        // This is part of the shadowing factor S from Eqn. 6
        
        // the direction of interest v, is the lightDir
        const float cosTheta_Zl = dot(zenith, normalize(gSky.lightDir));
        const float3 lightTransmittance = GetLightTransmittance(transmittanceLUT, lutSampler, atmosphere, r, cosTheta_Zl);

        // pu is the isotropic phase function value, already pre-defined in isotropicPhase

        // E_I is source illuminance, set as 1.0

        const float3 lightSourceL = gSky.sunIlluminance;
        
        const float3 curLuminance = lightSourceL * (lightTransmittance * isotropicPhase * scattering);
        
        // Normally, we'd just perform TODO
        //
        // However, according to slide 28 in Hillaire's 2015 presentation:
		// http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
        //
        // It's inaccurate to directly use curLuminance (which is calculated using samplePos, the position at the end of
        // the sample segment) and add curLuminance * transmittance.
        //
        // Instead, we should use lum_integral, which is curLuminance integrated along the sample segment w.r.t.
        // the transmittance over dt - then we add lum_integral * transmittance.
        //
        // This integral encapsulates TODO
        //
        // This integral can be analytically computed as so:
        const float3 integralOverSegment_lum = (curLuminance - curLuminance * sampleTransmittance) / sampleMedium.extinction;

        // Add to L-prime integral (Eq. 6)
        // where integralOverSegment_lum * transmittance is the integrand
        luminance += integralOverSegment_lum * transmittance;

        // Calculating Lf integrand (Eq. 8):
        //
        // For the same reason above, when integrating the scattering value, we will use the analytical integral over
        // the segment w.r.t the sampleTransmittance
        const float3 curEnergyTransfer = scattering * 1.;
        const float3 integralOverSegment_energy = (curEnergyTransfer - curEnergyTransfer * sampleTransmittance) / sampleMedium.extinction;

        // Add to Lf integral (Eq. 8), integrand = integralOverSegment_energy * transmittance
        energyTransfer += integralOverSegment_energy * transmittance;
        
        // update transmittance
        transmittance *= sampleTransmittance;
    }

    // Add T(x,p)L_0(p,v) from Eqn. 6, where L_0 is the luminance contribution
    // from the light reflected off the ground
    if(hitGround) {
        const float3 groundPos = rayOrigin + distToGround * rayDir;
        const float r = length(groundPos); // This should just equal Rb... right?
        const float3 zenith = groundPos / r;
        const float cosTheta_Zl = dot(zenith, normalize(gSky.lightDir));
        
        const float3 lightTransmittance = GetLightTransmittance(transmittanceLUT, lutSampler, atmosphere, r, cosTheta_Zl);

        // the up vector is also the normal of the ground
        // whose cosine we need to calculate the luminance reflected off the
        // ground's surface towards the camera
        const float cosTheta_Nl = saturate(cosTheta_Zl); // we clamp the cosine in the range [0,1] since we don't want a negative light contribution
        luminance += gSky.sunIlluminance * lightTransmittance * transmittance * cosTheta_Nl * gSky.groundAlbedo / PI;
    }
    
    integratedLuminance = luminance;
    integratedEnergyTransfer = energyTransfer;
}


[RootSignature( MultiScatteringLUT_RootSignature )]
[numthreads(32, 32, 1)] // define the cells in a thread group
void main( ComputeShaderInput IN ) {
    const uint3 Cell = IN.DispatchThreadID;

    // x,y,z mapped to [0,1]
    float TexWidth, TexHeight;
    multiscatteringLUT.GetDimensions(TexWidth, TexHeight);
    
    const float2 uv = float2((float)Cell.x,(float)Cell.y) / float2(TexWidth, TexHeight);

    // cosine(angle between zenith and light source)
    float cosTheta_Zl;
    
    // radius of X (distance from the center of the planet)
    float r;

    // set cosTheta_Zl and r
    UVToMultiScatteringParameters(atmosphere, uv, cosTheta_Zl, r);

    // a simple point with radius r
    const float3 startPos = float3(0,0,r);
    const float3 lightDir = float3(0, sqrt(1 - cosTheta_Zl * cosTheta_Zl), cosTheta_Zl);
    
    // At each point X, calculate incoming illuminance around X using
    // uniformly distributed rays around the sphere, centered at X.

    // The "mathematics convention" for spherical coordinates is used here.
    // polar angle = phi, in range[0, pi]
    // azimuthal angle = theta, in range [0, 2pi]

    const float sqrtSample = SQRTSAMPLECOUNT; // need this as a float
    const float invSamples = 1.0/(sqrtSample*sqrtSample);
    const float totalSamples = sqrtSample * sqrtSample;

    float3 secondOrderL = 0.0;
    float3 fms = 0.0;

    // The domain of integrals in (Eq. 5) and (Eq. 7), (i.e. over a sphere)
    // are represented by the interpolation of theta and phi (spherical coordinates)
    //
    // The integrands L' from Eq. 6 and Lf from Eq. 8 are directly calculated using CalculateIlluminanceEnergyTranfer()
    
    // There will be (SQRTSAMPLECOUNT * SQRTSAMPLECOUNT) iterations
    // Aside: In Hillaire's implementation, each evaluation of L' and Lf was done in parallel with 
    //        compute shaders in a thread group with dimensions (1,1,64) which allows one to
    //        avoid this for loop.
    for(float i = 0; i < sqrtSample; i+=1.0) {
        for(float j = 0; j < sqrtSample; j+=1.0){
            // uv are percentages of how far phi and theta are, in their range
            const float u = (i / sqrtSample);
            const float v = (j / sqrtSample);
            
            // Correctly generate uniformly distributed points along sphere, as in Hillaire's implementation.
            // see https://mathworld.wolfram.com/SpherePointPicking.html
            const float theta = 2 * PI * u;
            const float phi = acos(2 * v - 1);

            // Transform spherical coordinates to a Cartesian direction
            float3 rayDir;
            rayDir.x = cos(theta) * sin(phi);
            rayDir.y = sin(theta) * sin(phi);
            rayDir.z = cos(phi);

            // illuminance coming from the rayDir direction
            float3 L_prime;

            // energy transfer factor
            float3 Lf;

            // set L and fms
            IntegrateLuminanceAndEnergyTransfer(startPos, rayDir, L_prime, Lf);

            secondOrderL += L_prime * (1.0 / totalSamples);
            fms += Lf * (1.0 / totalSamples);
        }
    }

    // isotropicPhase = pu = 1/4pi (see section 4 in Hillaire's paper)
    const float isotropicPhase = 1 / (4 * PI);
    
    // L_{2nd order} integral is multiplied by the isotropic phase, pu, a constant. See Eq. 7.
    secondOrderL = secondOrderL * isotropicPhase;

    // f_ms is also multiplied by the same constant, pu. See Eq. 8.
    fms = fms * isotropicPhase;

    // F_ms as defined in Eq 9.
    //
    // F_ms = "... the infinite multiple scattering light contribution factor, as a geometric series infinite sum"
    //      = 1 + fms + fms^2 + fms^3 + ...
    //      = (1 / 1 - fms)
    const float3 F_ms = (1.0 / (1.0 - fms));

    // psi_ms as defined in Eq. 10.
    //
    // psi_ms = "The total contribution of a directional light with an infinite number of scattering orders" (Hillaire)
    const float3 psi_ms = secondOrderL * F_ms;

    multiscatteringLUT[Cell.xy] = float4(psi_ms * 1., 1.0);
}
