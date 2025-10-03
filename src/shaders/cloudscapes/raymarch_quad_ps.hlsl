#include "common/math.hlsl"
#include "common/volumetric_rendering.hlsl"
#include "common/render_common.hlsl"
#include "atmosphere/atmosphere_common.hlsl"

struct PSIn {
    float2 UV : UV;
};

Texture3D<float4> CloudModelNoise : register(t0);
Texture3D<float4> CloudDetailNoise : register(t1);

Texture2D<float4> blueNoise : register(t2);
Texture2D<float4> weatherTexture : register(t3);
Texture2D<float4> skyViewLUT: register(t4);

Texture2D<float4> prevFrame : register(t5);

SamplerState Sampler : register(s0);
SamplerState prevFrameSampler : register(s1);

ConstantBuffer<RenderContext> renderContext : register(b0);

struct CloudParameters {
    float3 lightColor;
    float phaseG;
    
    float modelNoiseScale; // 
    float cloudCoverage; //
    float highFreqScale;
    float highFreqModScale;
    
    float highFreqHFScale;
    float largeDtScale;
    float extinction;
    int numSamples;

    float4 beersScale;

    float2 weatherRadius;
    float minWeatherCoverage;
    int useBlueNoise;
    
    int fixedDt;
    float3 pad0;

    int useAlpha;
    float3 windDir;

    float windSpeed;
    float3 pad1;
    
    float4 lodThresholds;
    
    float innerShellRadius;
    float outerShellRadius;
    float2 pad2;

    float3 lightDir; // posToLight
    float pad3;
};

ConstantBuffer<CloudParameters> cloudParams : register(b1);

float rand(float x) {
    return frac(sin(x) * 100000.f);
}

float rand3d(float3 x) {
    // 0-1
    return frac(sin(dot(x, float3(12, 67, 411))) * 42123.);
}

// "Remaps" a value in one range into another range
float Remap(float Val, float OldMin, float OldMax, float NewMin, float NewMax) {
    float p = (Val - OldMin) / (OldMax - OldMin);
    return NewMin + p * (NewMax - NewMin);
}

// Get the blended density gradient for 3 different cloud types
// - relativeHeight is normalized distance from inner to outer atmosphere shell
// - cloudType is read from cloud placement blue channel
//      - meaning, "cloud type" is being controlled by a separate texture and not manually by a cpu-side variable
//
// This can also be defined as pre-baked "curves".
float CloudLayerDensity(float relativeHeight, float cloudType) {
    relativeHeight = clamp(relativeHeight, 0, 1);
    
    float cumulus = max(0.0, Remap(relativeHeight, 0.0, 0.2, 0.0, 1.0) * Remap(relativeHeight, 0.7, 0.9, 1.0, 0.0));
    float stratocumulus = max(0.0, Remap(relativeHeight, 0.0, 0.2, 0.0, 1.0) * Remap(relativeHeight, 0.2, 0.7, 1.0, 0.0)); 
    float stratus = max(0.0, Remap(relativeHeight, 0.0, 0.1, 0.0, 1.0) * Remap(relativeHeight, 0.2, 0.3, 1.0, 0.0)); 

    float d1 = lerp(stratus, stratocumulus, clamp(cloudType * 2.0, 0.0, 1.0));
    float d2 = lerp(stratocumulus, cumulus, clamp((cloudType - 0.5) * 2.0, 0.0, 1.0));
    return lerp(d1, d2, cloudType);
}

#define INNER_SHELL_RADIUS cloudParams.innerShellRadius //1.5
#define OUTER_SHELL_RADIUS cloudParams.outerShellRadius //5.0

float GetHeightFraction(float r) {
    const float innerShellRadius = 6360 + INNER_SHELL_RADIUS; // in km
    const float outerShellRadius = 6360 + OUTER_SHELL_RADIUS; // in km

    float h = (r - innerShellRadius) / (outerShellRadius - innerShellRadius);
    return h;
}


float DensityHeight(float r) {
    float h = GetHeightFraction(r);
    
    float2 Range1 = float2(0.01, 0.3);
    float P1 = clamp((h - Range1.x) / (Range1.y - Range1.x), 0., 1.);
    float V1 = lerp(0., 1., P1);
    
    float2 Range2 = float2(0.8, 1.0);
    float P2 = clamp((h - Range2.x) / (Range2.y - Range2.x), 0., 1.);
    float V2 = lerp(1., 0., P2);

    return V1 * V2;
}

float HeightBiasCoverage(float coverage, float height) {
    return pow(coverage, clamp(Remap(height, 0.7, 0.8, 1.0, 0.8), 0.8, 1.0));
}

float GetCloudDensityByPos(float3 Pos, float3 sampleOffset, bool HighQuality, int mip) {
    float posRadius = length(Pos);
    const float heightFraction = GetHeightFraction(length(Pos));
    if(heightFraction > 1 || heightFraction < 0) {
        return 0.0;
    }

    float3 samplePos = Pos + sampleOffset;
    samplePos.z -= 6360;

    const float modelScale = cloudParams.modelNoiseScale;
    const float highFreqScale = cloudParams.highFreqScale;
    const float highFreqHeightFractionScale = cloudParams.highFreqHFScale;
    const float highFreqRemapScale = cloudParams.highFreqModScale;

    const float2 weatherRadius = cloudParams.weatherRadius; // float2(500,500); // km
    const float2 weatherUV = (samplePos.xy + float2(150, 0) + weatherRadius / 2.) / weatherRadius;

    // This weather texture tells us
    // - different cloud types in the world,
    // - density scalars (maybe for rain clouds)
    // - etc.
    const float3 weatherData = weatherTexture.SampleLevel(Sampler, weatherUV, 0).rgb;
    
    float4 Noises = CloudModelNoise.SampleLevel(Sampler, samplePos * modelScale, mip);

    float LowFreqFBM = Noises.g * 0.625 + Noises.b * 0.25 + Noises.a * 0.125;
    float PerlinWorley = Noises.r;
    float BaseCloud = Remap(PerlinWorley, LowFreqFBM - 1., 1.0, 0.0, 1.0);

    // Based on the altitude of the sample, we can determine which "cloud type" we want
    //
    // It's important to note that all we're doing is changing the way we sample the data (the noise)
    // based on altitude, and current weather patterns (according to the weatherTexture).
    // Changing the way we sample the source data, we can directly control how the clouds get formed.
    // "How they get formed" directly corresponds to: the sampled densities (values from 0 to 1)
    // when we're doing the ray marching
    BaseCloud *= CloudLayerDensity(heightFraction, weatherData.b);

    float CloudCoverage = HeightBiasCoverage(min(cloudParams.minWeatherCoverage, weatherData.r), heightFraction); //);weatherData.r; // min(cloudParams.minWeatherCoverage, weatherData.r); //min(0.8, weatherData.r); //cloudParams.cloudCoverage; // + rand3d(Pos) * 0.3;smoothstep(0.4, 0.8, BaseCloud_2); //0.9; //Remap(Noises2.z, 0, 1, 0.8, 0.93);
    
    float BaseCloudWithCoverage = Remap(BaseCloud, CloudCoverage, 1.0, 0.0, 1.0);
    BaseCloudWithCoverage *= CloudCoverage;

    float FinalCloud = BaseCloudWithCoverage;

    // We "append" a different noise to add more detail in the cloud if it's asked for
    // Since texture samples are expensive, (ideally) this should only occur for samples closer
    // to the camera. Additionally we might want to skip the detailed noise when doing the
    // "inner light ray march"
    if(HighQuality) {
        float3 HighFreqNoises = CloudDetailNoise.SampleLevel(Sampler, samplePos * highFreqScale, 0).rgb;

        float HighFreqFBM = HighFreqNoises.r * 0.625 + HighFreqNoises.g * 0.25 + HighFreqNoises.b * 0.125;

        float HighFreqNoiseModifier = lerp(HighFreqFBM,
            1.0 - HighFreqFBM, saturate(heightFraction * highFreqHeightFractionScale));

        FinalCloud = Remap(BaseCloudWithCoverage, HighFreqNoiseModifier * highFreqRemapScale, 1.0, 0.0, 1.0);
    }

    return saturate(FinalCloud);
}

float3 U2Tone(float3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;

    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

// debug colors:
#define BLUE float3(0, 0, 1)
#define GREEN float3(0, 1, 0)
#define RED float3(1, 0, 0)
#define DEBUG_RETURN(v) finalTransmittance = 0.0f; return v;

float3 CloudMarch(float3 rayOrigin, float3 rayDir, float rayOffset, float3 skyColor, out float3 finalTransmittance) {
    const float3 RANDOM_VECTORS[6] = {float3( 0.38051305f,  0.92453449f, -0.02111345f),float3(-0.50625799f, -0.03590792f, -0.86163418f),float3(-0.32509218f, -0.94557439f,  0.01428793f),float3( 0.09026238f, -0.27376545f,  0.95755165f),float3( 0.28128598f,  0.42443639f, -0.86065785f),float3(-0.16852403f,  0.14748697f,  0.97460106f)};

    // ===== start ray definition logic =======
    
    // Context:
    // Given our ray that we're about to march through (in round-world),
    // we are marching through only a subset of the atmosphere (where we define
    // clouds to be seen). This implies this ray is going through a "spherical shell".
    // "spherical shell is the three-dimensional region between two concentric spheres of different radii"
    //
    // The logic below is trying to figure out where the ray march should start and end.
    
    const float numSamples = cloudParams.numSamples;
    const float Rb = 6360;
    
    const float innerShellRadius = Rb + INNER_SHELL_RADIUS; // in km
    const float outerShellRadius = Rb + OUTER_SHELL_RADIUS; // in km

    float innerNearDist, innerFarDist;
    const uint numHitInnerShell = GetRaySphereDistances(rayOrigin, rayDir, float3(0,0,0), innerShellRadius, innerNearDist, innerFarDist);
    const bool hitInnerShell = numHitInnerShell > 0;

    float outerNearDist, outerFarDist;
    const uint numHitOuterShell = GetRaySphereDistances(rayOrigin, rayDir, float3(0,0,0), outerShellRadius, outerNearDist, outerFarDist);
    const bool hitOuterShell = numHitOuterShell > 0;

    // dist between shells
    float raySampleLength = 10.;

    // ray hit both shells:
    //      - Looking outward, from inside the inner shell (hit both shells once)
    //      - Looking inward, from outside the shell, below the inner shell horizon (hit both shells twice)
    //      - Looking inward, from inside shell, below inner shell horizon (hit inner twice, hit outer once)
    if(hitInnerShell && hitOuterShell) {
        if(numHitOuterShell == 1 && numHitInnerShell == 1) {
            raySampleLength = abs(outerNearDist - innerNearDist);
        }
        else if(numHitOuterShell == 2 && numHitInnerShell == 2) {
            raySampleLength = abs(outerNearDist - innerNearDist);
        }
        else if(numHitOuterShell == 1 && numHitInnerShell == 2) {
            raySampleLength = innerNearDist + abs(outerNearDist - innerNearDist);
        }
    }
    
    // hit outer shell only, then:
    //      - We're looking inward from outside the shell, above the horizon of the inner shell (hit twice)
    //      - We're inside the cloud shell, looking outward (hit once)
    else if(hitOuterShell) {
        if(numHitInnerShell == 0) {
            raySampleLength = abs(outerFarDist - outerNearDist);
        }
        else if(numHitInnerShell == 1) {
            raySampleLength = abs(outerNearDist); 
        }
    }
    
    // NOTE: ray cannot hit inner and not hit outer
    
    const float maxSampleLength = cloudParams.beersScale.w;
    raySampleLength = min(raySampleLength, maxSampleLength);
    
    // ===== end ray definition logic =======

    // Now that we have the length of the ray: we can define
    // what is our "step delta". This is purely opinionated,
    // but has significant impact on how the cloud densities are sampled.
    //
    // E.g. very small step sizes may completely miss a big cloud that is visible
    // 3km away. Whereas big step sizes have the chance of skipping a thin cloud entirely.
    // It's a balancing act.
    float stepSize;
    if(cloudParams.fixedDt) {
        stepSize = cloudParams.lodThresholds.x; // * (OUTER_SHELL_RADIUS - INNER_SHELL_RADIUS) / numSamples; //;raySampleLength / numSamples;
    }
    else {
        stepSize = raySampleLength / numSamples; //5.2 * (OUTER_SHELL_RADIUS - INNER_SHELL_RADIUS) / numSamples; //;raySampleLength / numSamples;
    }

    
    const float largeStepSize = stepSize * cloudParams.largeDtScale;

    float groundNearDist, groundFarDist;
    const uint numHitGround = GetRaySphereDistances(rayOrigin, rayDir, float3(0,0,0), Rb, groundNearDist, groundFarDist);
    const bool hitGround = numHitGround > 0;

    const bool hitGroundFirst = hitGround && groundNearDist < innerNearDist;
    const bool noIntersection = !hitInnerShell && !hitOuterShell && !hitGround;
    const bool hitOuterOnly = !hitInnerShell && hitOuterShell;
    const bool inBetweenShells = length(rayOrigin) > innerShellRadius && length(rayOrigin) < outerShellRadius;
    // const bool underGround = length(rayOrigin) < Rb;

    float3 transmittance = 1.;
    float3 L = 0;
    float t = 0.0; // cur distance from rayOrigin

    const bool radianceValid = !noIntersection && !hitGroundFirst;

    //
    const float minT = (hitInnerShell && hitOuterShell)? min(outerNearDist, innerNearDist) : hitInnerShell? innerNearDist : outerNearDist;
    float maxT = hitGround? groundNearDist : (hitInnerShell && hitOuterShell)? max(outerNearDist, innerNearDist) : numHitOuterShell == 1? outerNearDist : outerFarDist;

    if(inBetweenShells) {
        t = 0;
        
        if(numHitInnerShell <= 0) {
            maxT = outerNearDist;
        }
        else {
            maxT = innerNearDist;
        }
        
    }
    else if(length(rayOrigin) < innerShellRadius) {
        t = innerNearDist;
        maxT = outerNearDist;
    }
    else if(length(rayOrigin) > outerShellRadius) {
        t = outerNearDist;

        if(numHitInnerShell <= 0) {
            maxT = outerFarDist;
        }
        else {
            maxT = innerNearDist;
        }
    }
    
    t += stepSize * rayOffset;

    // extinction is the "absorption" of each cloud particle.
    // The more extinction => the "denser" each cloud particle => the darker it can get
    const float extinction = cloudParams.extinction;
    const float scattering = extinction / 2.;

    // pos to light
    const float3 lightDir = normalize(cloudParams.lightDir); //normalize(float3(0, -1, 1.));

    // Phase describes how light is going to bounce off each "cloud particle" we're going
    // to march through. Combining 2 phase functions is claimed to be "more realistic"
    // which is what's happening here. The final miePhase variable is directly used in
    // the final light calculation per march.
    const float cosTheta_lv = dot(-lightDir, -rayDir);
    const float miePhase1 = MiePhaseApproximation_HenyeyGreenstein(cosTheta_lv, -0.2);
    const float miePhase2 = MiePhaseApproximation_HenyeyGreenstein(cosTheta_lv, 0.9);
    const float miePhase = lerp(miePhase1, miePhase2, cloudParams.phaseG);

    const float lightAlpha = saturate(dot(lightDir, float3(0,0,1)));

    int largeDtThreshold = 6; 
    int ogLargeDtThreshold = largeDtThreshold;
    int numDensityZero = 0; 
    int totalPositiveDensity = 0;
    
    // state to skip
    bool skip = false;
    bool reachedEnd = false;

    float largeDtBase = cloudParams.largeDtScale;
    float largeDt = largeDtBase;

    float prevDensity = 0.0;
    

    for(float i = 0; i < numSamples; i += 1.) {
        const bool isSearching = numDensityZero > largeDtThreshold;

        float uniformDt = stepSize; //lerp(start, end, alpha);

        // blue-noise breaks up "banding artifacts" when we sample, this is purely for
        // dealing with visual artifacts and not a core part of the cloud rendering technique.
        // For learning purposes, assume that blueRand isn't doing anything important.
        float blueRand = blueNoise.SampleLevel(Sampler, cloudParams.beersScale.x * (rayOrigin + t * rayDir).xy, 0).r;
        if(!cloudParams.useBlueNoise) {
            blueRand = 0;
        }
        
        // uniformDt *= (1. / (min(2., numDensityPos + 1)));
        largeDt = largeDtBase; //largeDtBase * maxT; //pow(cloudParams.lodThresholds.y, numDensityZero); //clamp(numDensityZero, 0, largeDtThreshold));
        largeDt += largeDt * blueRand;

        // To skip over empty space, we dynamically change the step delta to be large
        // If we're currently detecting there are no clouds (i.e. we've only been sampling 0s for
        // cloud density), then we make the step delta large.
        // If we're currently IN a cloud (cloud density > 0), then we continue with our "smaller" step sizes.
        const float dt = isSearching? largeDt: uniformDt + uniformDt * blueRand;
        t += dt; // t is: "how far along the ray are we?"

        // maxT is the end of our ray. If we reached the end, then reachedEnd signifies we shouldn't
        // be calculating any lighting/color anymore.
        //
        // We DO NOT break like in normal cpu-side programming because there are many instances of this
        // shader being ran currently, in lock-step. The more similar the logic is per instance, the
        // more performant this shader will be.
        if(t > maxT) {
            reachedEnd = true;
        }

        // No more light is passing through all the cloud particles we sampled. So we've already
        // reached our "final color"
        if(transmittance.x < .01) {
            reachedEnd = true;
        }

        const float3 samplePos = rayOrigin + t * rayDir;
        
        const float3 sampleOffset = cloudParams.windSpeed * renderContext.time * cloudParams.windDir;

        const float4 b = cloudParams.beersScale;
        // int mip = t > b.x? 2 : t > b.y? 1 : 0;
        // int mip = isSearching? 1 : alpha > 0.5? 2 : 1;

        int mip = 0;
        
        // Here we sample the "noise" 3D textures based on where we currently are in the world
        const float density = GetCloudDensityByPos(samplePos, sampleOffset, !isSearching, mip);
        prevDensity = density;

        // We're currently not in a cloud, let's keep track of that
        if(density <= 0.01) {
            numDensityZero++; //= max(largeDtThreshold, numDensityZero + 1);
        }

        // We're in a cloud!
        if(density > 0.01) {
            
            if(isSearching) {
                skip = true;
                t -= dt;
                //largeDtThreshold = ogLargeDtThreshold * 0.5f;
            }
            numDensityZero = 0;
            totalPositiveDensity += 1;
            
            const float3 sampleTransmittance = exp(-1. * (extinction * density * dt));

            // ==== start light ray calculations
            //
            // Sample densities towards the light source to figure out how "lit"
            // this piece of the cloud is. This is one of the biggest factors in
            // why this technique can get expensive. 
            const float lightDistToOuter = GetNearestRaySphereDistance(samplePos, lightDir, float3(0,0,0), outerShellRadius);
            const float lightDistToInner = GetNearestRaySphereDistance(samplePos, lightDir, float3(0,0,0), innerShellRadius);

            float maxLightLen = 0;
            if(lightDistToOuter > 0) {
                maxLightLen = lightDistToOuter;
            }
            if(lightDistToInner > 0 && lightDistToInner < lightDistToOuter) {
                maxLightLen = lightDistToInner;
            }
            
            const float numLightSamples = 6.;
            const float lightSampleLength = cloudParams.beersScale.z; // not actually "beersScale", just needed a quick way to tune values
            float lightT = 0.0;
            float lightDensity = 0.0;
            float light_dt = lightSampleLength / numLightSamples;
            
            // ==== end light ray calculations

            for(float j = 0.; j < numLightSamples; j += 1.) {
                const float newLightT = lightSampleLength * (j + 0.1) / numLightSamples;
                const float lightDt = abs(newLightT - lightT);
                lightT += lightDt;

                // The usage of RANDOM_VECTORS is to sample in a "cone" instead of linearly
                // towards the sun.
                const float3 lightSamplePos = samplePos + lightT * (lightDir + RANDOM_VECTORS[j] * j);
                
                const float curLightDensity = GetCloudDensityByPos(lightSamplePos, sampleOffset, j < 3, mip);
                lightDensity += curLightDensity;
            }

            // ===== start light contribution =====
            //
            // The following uses cloud density and the samples we took towards the sun
            // to figure out how "lit" this piece of the cloud is and how much of that light
            // will be transferred back to the eye. This is where "Phase" 
            // 
            const float ldt = light_dt;
            const float cd = lightDensity * ldt * extinction;
            
            const float beers = max(exp(-1 * cd), 0.7 * exp(-1 * 0.25 * cd));
            const float powShug = 2. * (1.0 - exp(-1 * cd * 2.0));
            float lightTransmittance = beers * powShug;
            
            float r_alpha = GetHeightFraction(length(samplePos));

            // Ambient here is the simplest form of "Multiscattering"
            // The SkyAtmosphere technique (which is not in this shader) is supposed to define a
            // 3D texture in which we can query "truer" multiscattering values. I was to lazy too
            // implement that. So here we are with a simple ambient scalar.
            float ambientMax = lerp(0.01, 10, lightAlpha);
            float3 ambient = cloudParams.beersScale.y * lerp(0.0, ambientMax, (r_alpha + 0.1));

            const float lightLuminance = lerp(0.001, 1, lightAlpha);
            
            const float3 curL = (ambient + skyColor * 1.5 + lightLuminance * (lightTransmittance * miePhase) * scattering) * density;

            float3 st = sampleTransmittance;

            // Simplified and more accurate integration by Sebastian Hillaire
            const float3 intS = (curL - curL * st);
            
            // ===== end light contribution =====

            // Only modify L if we need to
            if(!skip && !reachedEnd && radianceValid) {
                L += intS * transmittance;
                transmittance *= sampleTransmittance;
            }
            skip = false;
        }
    }

    finalTransmittance = transmittance;
    
    return L;
}

float4 main(PSIn In, float4 screen_pos : SV_Position): SV_Target {
    // This first part is to only render 1/4 of the scene each frame.
    // This is done because ray marching is so expensive. While doing
    // partial renders per frame is much more performant,
    // it comes with its own drawbacks - especially visually.
    const int bayerFilter[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    };

    int2 iscreen_pos = int2(screen_pos.xy);
    int index = renderContext.frame % 16;

    // 
    bool update = (((iscreen_pos.x + 4 * iscreen_pos.y) % 16)
            == bayerFilter[index]);

    float uvOffset = 0.;
    float2 uv = (screen_pos.xy + float2(uvOffset, uvOffset)) /
                renderContext.screenSize;

    // If we shouldn't update, then we should copy values from a previous frame.
    if(!update) {
        // float4 prevFrameVal = prevFrame.SampleLevel(prevFrameSampler, uv, 0);
        float4 prevFrameVal = prevFrame.Load(int3(screen_pos.x, screen_pos.y, 0));
        return float4(prevFrameVal.rgb, prevFrameVal.a);
    }

    // dx12, screen_pos (0,0) is top left and (1,1) is bottom right.
    // However, we want (0,0) to be bottom left and (1,1) to be top right
    // We can do this by inverting y (ie remap using 1-y => y=1  ("bottom") is remapped to 0 ("top"), and vice versa)
    float ar = (float) renderContext.screenSize.x / (float) renderContext.screenSize.y;
    uv.y = 1.0f - uv.y; // invert y
    uv.xy -= 0.5;
    uv.x *= ar;

    const float3 ndc = float3(uv.x, uv.y, 1.0);
    const float4 viewPos = mul(renderContext.invProjectionMat, float4(ndc, 1.0));
    const float3 rayDir = normalize(mul((float3x3) renderContext.invViewMat, viewPos.xyz / viewPos.w));
    const float3 worldPos = renderContext.cameraPos + float3(0., 0., 6360.);

    float3 skyColor = 0.0;
    {
        // querying the color of the sky, using the world position
        float3 queryDir = cloudParams.lightDir;
        
        const float r = length(worldPos);
        const float3 zenith = normalize(worldPos);
        const float cosTheta_Zv = dot(zenith, queryDir);

        const float3 sideDir = normalize(cross(zenith, queryDir));
        const float3 fwdDir = normalize(cross(sideDir, zenith));
        const float2 lightOnPlane = normalize(float2(dot(normalize(cloudParams.lightDir), fwdDir), dot(normalize(cloudParams.lightDir), sideDir)));
        const float cosTheta_lv = lightOnPlane.x;

        AtmosphereContext atmosphere;
        atmosphere.Rb = 6360;
        
        float2 skyView_uv;
        SkyViewParametersToUV(atmosphere, r, cosTheta_lv, cosTheta_Zv, skyView_uv);

        skyColor = skyViewLUT.Sample(Sampler, skyView_uv).rgb;
        //skyColor = ConvertToSRGB(skyColor);
    }

    float alpha;

    float rayOffset = blueNoise.Sample(Sampler, uv).r;
    if(!cloudParams.useBlueNoise) {
        rayOffset = 0;
    }

    // Transmittance here is being used as:
    // "how much light is able to pass through the clouds", as a percentage
    // And this value is directly correlated to the densities of the clouds
    // E.g. a very dense cloud will be completely opaque, no light from the background will "go through"
    // it, so all light is blocked by the clouds which implies a transmittance of 0.
    float3 transmittance;
    const float3 CloudColor = CloudMarch(worldPos, rayDir, rayOffset, skyColor, transmittance);
    alpha = transmittance.x;

    // HDR/Tonemapping
    // https://learnopengl.com/Advanced-Lighting/HDR
    float gamma = 2.2;
    float3 mapped = float3(1,1,1) - exp(-CloudColor * 3.0);
    mapped = pow(mapped, 1.0 / gamma);

    // NOTE: "alpha" here is being used in a different way than the normal alpha == 1 => completely opaque.
    // The final clouds scene is being displayed on top of the previous sky scene via the following combination
    //
    // final_color = old_color * (new_alpha) + new_color * (1 - new_alpha)
    //
    // So a fully opaque cloud is going to have alpha == 0. While no clouds is going to have alpha == 1.
    // This is just implementation details and not related to the actual cloud rendering technique.
    return float4(mapped, alpha);
}