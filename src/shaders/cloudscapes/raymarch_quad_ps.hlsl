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

float Remap(float Val, float OldMin, float OldMax, float NewMin, float NewMax) {
    float p = (Val - OldMin) / (OldMax - OldMin);
    return NewMin + p * (NewMax - NewMin);
}

float sdBox( float3 p, float3 b )
{
    float3 q = abs(p) - b;
    return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
}

float sdSphere( float3 p, float r) {
    return length(p) - r;
}

float StratusDensityHeight(float h) {
    float2 Range1 = float2(0.01, 0.02);
    float P1 = clamp((h - Range1.x) / (Range1.y - Range1.x), 0., 1.);
    float V1 = lerp(0., 1., P1);
    
    float2 Range2 = float2(0.09, 0.1);
    float P2 = clamp((h - Range2.x) / (Range2.y - Range2.x), 0., 1.);
    float V2 = lerp(1., 0., P2);

    return V1 * V2;
}

// Get the blended density gradient for 3 different cloud types
// relativeHeight is normalized distance from inner to outer atmosphere shell
// cloudType is read from cloud placement blue channel
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
    mip = 0;
    
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
    const float2 weatherUV = (samplePos.xy + weatherRadius / 2.) / weatherRadius;
    
    const float3 weatherData = weatherTexture.SampleLevel(Sampler, weatherUV, 0).rgb;
    
    float4 Noises = CloudModelNoise.SampleLevel(Sampler, samplePos * modelScale, mip);

    float LowFreqFBM = Noises.g * 0.625 + Noises.b * 0.25 + Noises.a * 0.125;
    float PerlinWorley = Noises.r;
    float BaseCloud = Remap(PerlinWorley, LowFreqFBM - 1., 1.0, 0.0, 1.0);

    BaseCloud *= CloudLayerDensity(heightFraction, weatherData.b);

    //float CloudCoverage = HeightBiasCoverage(heightFraction, min(cloudParams.minWeatherCoverage, weatherData.r)); //);weatherData.r; // min(cloudParams.minWeatherCoverage, weatherData.r); //min(0.8, weatherData.r); //cloudParams.cloudCoverage; // + rand3d(Pos) * 0.3;smoothstep(0.4, 0.8, BaseCloud_2); //0.9; //Remap(Noises2.z, 0, 1, 0.8, 0.93);
    float CloudCoverage = HeightBiasCoverage(min(cloudParams.minWeatherCoverage, weatherData.r), heightFraction); //);weatherData.r; // min(cloudParams.minWeatherCoverage, weatherData.r); //min(0.8, weatherData.r); //cloudParams.cloudCoverage; // + rand3d(Pos) * 0.3;smoothstep(0.4, 0.8, BaseCloud_2); //0.9; //Remap(Noises2.z, 0, 1, 0.8, 0.93);
    
    float BaseCloudWithCoverage = Remap(BaseCloud, CloudCoverage, 1.0, 0.0, 1.0);
    BaseCloudWithCoverage *= CloudCoverage;

    float FinalCloud = BaseCloudWithCoverage;
    
    if(HighQuality) {
        float3 HighFreqNoises = CloudDetailNoise.SampleLevel(Sampler, samplePos * highFreqScale, 0).rgb;

        float HighFreqFBM = HighFreqNoises.r * 0.625 + HighFreqNoises.g * 0.25 + HighFreqNoises.b * 0.125;

        float HighFreqNoiseModifier = lerp(HighFreqFBM,
            1.0 - HighFreqFBM, saturate(heightFraction * highFreqHeightFractionScale));

        FinalCloud = Remap(BaseCloudWithCoverage, HighFreqNoiseModifier * highFreqRemapScale, 1.0, 0.0, 1.0);
    }

    return saturate(FinalCloud);
}

float GetCloudDensity(float3 UVW, bool HighQuality) {
    
    float4 Noises = CloudModelNoise.Sample(Sampler, UVW * 6.5);
    float4 Noises2 = CloudModelNoise.Sample(Sampler, float3(UVW.x * 0.09, UVW.y * 0.07, UVW.z * 0.1));

    float LowFreqFBM = Noises.g * 0.625 + Noises.b * 0.25 + Noises.a * 0.125;
    float PerlinWorley = Noises.r;
    float BaseCloud = Remap(PerlinWorley, LowFreqFBM - 1., 1.0, 0.0, 1.0);

    BaseCloud *= DensityHeight(UVW.z);
    //BaseCloud *= StratusDensityHeight(UVW.z);

    float CloudCoverage = smoothstep(0.88, 0.96, Noises2.b); // Remap(Noises2.r, 0, 1, 0.8, 0.93);
    float BaseCloudWithCoverage = Remap(BaseCloud, CloudCoverage, 1.0, 0.0, 1.0);
    BaseCloudWithCoverage *= CloudCoverage;

    float FinalCloud = BaseCloudWithCoverage;
    if(HighQuality) {
        float3 HighFreqNoises = CloudDetailNoise.Sample(Sampler, float3(UVW * 5.5)).rgb;

        float HighFreqFBM = HighFreqNoises.r * 0.625 + HighFreqNoises.g * 0.25 + HighFreqNoises.b * 0.125;
        float HeightFraction = UVW.z;

        float HighFreqNoiseModifier = lerp(HighFreqFBM,
            1.0 - HighFreqFBM, saturate(HeightFraction * 10.0f));

        FinalCloud = Remap(BaseCloudWithCoverage, HighFreqNoiseModifier * 0.2, 1.0, 0.0, 1.0);
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

float3 CloudMarch(float3 rayOrigin, float3 rayDir, float rayOffset, float3 skyColor, out float3 finalTransmittance) {
    const float3 RANDOM_VECTORS[6] = {float3( 0.38051305f,  0.92453449f, -0.02111345f),float3(-0.50625799f, -0.03590792f, -0.86163418f),float3(-0.32509218f, -0.94557439f,  0.01428793f),float3( 0.09026238f, -0.27376545f,  0.95755165f),float3( 0.28128598f,  0.42443639f, -0.86065785f),float3(-0.16852403f,  0.14748697f,  0.97460106f)};
    
    const float numSamples = cloudParams.numSamples; // 256.;
    const float Rb = 6360;
    
    const float innerShellRadius = Rb + INNER_SHELL_RADIUS; // in km
    const float outerShellRadius = Rb + OUTER_SHELL_RADIUS; // in km
    
    const float innerNearDist = GetNearestRaySphereDistance(rayOrigin, rayDir, float3(0,0,0), innerShellRadius);
    const bool hitInnerShell = innerNearDist > 0;

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
        if(outerFarDist == -1) {
            raySampleLength = abs(innerNearDist);
        }
        else {
            raySampleLength = abs(outerNearDist - innerNearDist);
        }
    }
    
    // hit outer shell only, then:
    //      - We're looking inward from outside the shell, above the horizon of the inner shell (hit twice)
    //      - We're inside the cloud shell, looking outward (hit once)
    else if(hitOuterShell) {
        if(outerFarDist == -1) {
            raySampleLength = abs(outerNearDist); 
        }
        else {
            raySampleLength = abs(outerFarDist - outerNearDist); 
        }
    }
    
    // NOTE: ray cannot hit inner and not hit outer
    
    const float maxSampleLength = cloudParams.beersScale.w;
    raySampleLength = min(raySampleLength, maxSampleLength);

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

    const bool radianceValid = !noIntersection;

    //
    const float minT = (hitInnerShell && hitOuterShell)? min(outerNearDist, innerNearDist) : hitInnerShell? innerNearDist : outerNearDist;
    const float maxT = hitGround? groundNearDist : (hitInnerShell && hitOuterShell)? max(outerNearDist, innerNearDist) : numHitOuterShell == 1? outerNearDist : outerFarDist;
    
    if(numHitGround == 1 && groundNearDist > Rb) {
        t = maxT;
    }
    else if(inBetweenShells) {
        t = 0;
    }
    else {
        t = minT;
    }

    t += stepSize * rayOffset;

#if 1 
    const float extinction = cloudParams.extinction;
    const float scattering = extinction / 2.;

    // pos to light
    const float3 lightDir = normalize(cloudParams.lightDir); //normalize(float3(0, -1, 1.));

    const float cosTheta_lv = dot(-lightDir, -rayDir);
    const float miePhase1 = MiePhaseApproximation_HenyeyGreenstein(cosTheta_lv, -0.2);
    const float miePhase2 = MiePhaseApproximation_HenyeyGreenstein(cosTheta_lv, 0.9);
    const float miePhase = lerp(miePhase1, miePhase2, cloudParams.phaseG);

    const float lightAlpha = saturate(dot(lightDir, float3(0,0,1)));

    int largeDtThreshold = 6; //(int) cloudParams.lightColor.y;
    int ogLargeDtThreshold = largeDtThreshold;
    int numDensityZero = 0; //largeDtThreshold + 1;
    int totalPositiveDensity = 0;
    
    
    // state to skip
    bool skip = false;
    bool reachedEnd = false;

    float largeDtBase = cloudParams.largeDtScale;
    float largeDt = largeDtBase;

    float prevDensity = 0.0;
    

    for(float i = 0; i < numSamples; i += 1.) {
        const bool isSearching = numDensityZero > largeDtThreshold;

        float pRaySampled = (t - minT) / raySampleLength;
        float remainingLen = (1. - pRaySampled) * raySampleLength;
        float remainingSamples = numSamples - i;
        
        //float uniformDt = stepSize * cloudParams.lodThresholds.w; //remainingLen * (1. / remainingSamples);
        // float largeDt = uniformDt * cloudParams.largeDtScale;
        
        //float uniformDt = stepSize; //remainingLen * (1. / remainingSamples);
        float start = cloudParams.lodThresholds.z;
        float end = cloudParams.lodThresholds.w;

        float alpha = t / (cloudParams.lightColor.z * maxT);
        float uniformDt = start; //lerp(start, end, alpha);
        
        float blueRand = blueNoise.SampleLevel(Sampler, cloudParams.beersScale.x * (rayOrigin + t * rayDir).xy, 0).r;
        //blueRand = frac(blueRand + frac(renderContext.time) * 0.61803398875f);
        // blueRand = blueRand * 2- end/2;
        
        // uniformDt *= (1. / (min(2., numDensityPos + 1)));
        largeDt = largeDtBase * maxT; //pow(cloudParams.lodThresholds.y, numDensityZero); //clamp(numDensityZero, 0, largeDtThreshold));
        largeDt += largeDt * blueRand;

        const float dt = isSearching? largeDt: uniformDt + uniformDt * blueRand;
        t += dt;

        if(t > maxT) {
            reachedEnd = true;
        }

        if(length(transmittance) < .1) {
            reachedEnd = true;
        }

        //const float newT = minT + raySampleLength * (i + rayOffset) / numSamples + dtScale;
        //const float dt = abs(newT - t);
        //t = newT;

        const float3 samplePos = rayOrigin + t * rayDir;

        if(length(samplePos) < Rb) {
            reachedEnd = true;
        }
        
        const float3 sampleOffset = cloudParams.windSpeed * renderContext.time * cloudParams.windDir;

        const float4 b = cloudParams.beersScale;
        // int mip = t > b.x? 2 : t > b.y? 1 : 0;
        // int mip = isSearching? 1 : alpha > 0.5? 2 : 1;
        int mip = 0;
        const float density = GetCloudDensityByPos(samplePos, sampleOffset, !isSearching, mip);
        prevDensity = density;

        if(density <= 0) {
            numDensityZero++; //= max(largeDtThreshold, numDensityZero + 1);
        }

        if(density > 0) {
            
            if(isSearching) {
                skip = true;
                t -= dt;
                // largeDtThreshold = ogLargeDtThreshold * 0.5f;
            }
            numDensityZero = 0;
            totalPositiveDensity += 1;
            
            const float3 sampleTransmittance = exp(-1. * (extinction * density * dt));

            // integrate light ray
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
            const float lightSampleLength = cloudParams.beersScale.z; //dt * cloudParams.beersScale.z;
            float lightT = 0.0;
            float lightDensity = 0.0;
            float light_dt = cloudParams.beersScale.z * maxLightLen / numLightSamples;

            for(float j = 0.; j < numLightSamples; j += 1.) {
                const float newLightT = lightSampleLength * (j + 0.1) / numLightSamples;
                const float lightDt = abs(newLightT - lightT);
                lightT += lightDt;

                const float3 lightSamplePos = samplePos + lightT * (lightDir + RANDOM_VECTORS[j] * j);
                // const float3 lightSamplePos = samplePos + light_dt * j * lightDir;
                const float curLightDensity = GetCloudDensityByPos(lightSamplePos, sampleOffset, j < 3, mip);
                lightDensity += curLightDensity;
            }

            /*
            {
                const float newLightT = lightSampleLength * 3.0;
                const float lightDt = abs(newLightT - lightT);
                lightT += lightDt;

                const float3 lightSamplePos = samplePos + lightT * (lightDir + RANDOM_VECTORS[0]);
                const float curLightDensity = GetCloudDensityByPos(lightSamplePos, sampleOffset, false, mip);
                lightDensity += curLightDensity;
            }
            */

            const float ldt = light_dt; //lightSampleLength / numLightSamples;
            const float cd = lightDensity * ldt * extinction;
            
            const float beers = max(exp(-1 * cd), 0.7 * exp(-1 * 0.25 * cd));
            const float powShug = 2. * (1.0 - exp(-1 * cd * 2.0));

            //const float lightTransmittance = exp(-1 * cloudParams.lightColor.y * lightDensity); // * lightDensity); // beers * powShug;j < 3
            float lightTransmittance = beers * powShug;
            //lightTransmittance += blueRand * 0.3;
            
            const float3 lightColor = cloudParams.lightColor;
            // const float3 curL = lightColor * (lightTransmittance * miePhase * scattering * density) * dt;
            
           // const float mp = MiePhaseApproximation_HenyeyGreenstein(dot(sunToPos, -rayDir), cloudParams.phaseG);
            //const float mp = MiePhaseApproximation_HenyeyGreenstein(dot(sunToPos, -rayDir), cloudParams.phaseG);
            float r_alpha = GetHeightFraction(length(samplePos));

            float ambientMax = lerp(0.2, 2, lightAlpha);
            float3 ambient = cloudParams.beersScale.y * lerp(0.0, ambientMax, (r_alpha + 0.1));

            const float lightLuminance = lerp(0.01, 1., lightAlpha);
            
            const float3 curL = (ambient + skyColor * cloudParams.beersScale.w + lightLuminance * (lightTransmittance * miePhase) * scattering) * density;

            const float3 stMax = exp(-1. * (cloudParams.beersScale.w * extinction * dt));
            float3 st = sampleTransmittance; //max(sampleTransmittance, stMax);
            const float3 intS = (curL - curL * st) / (density);

            if(!skip && !reachedEnd && radianceValid) {
                L += intS * transmittance;
                //L += lightColor;
                //float c = saturate(cos(dot(rayDir, lightDir)));
                //L += transmittance * lerp(RED, BLUE, c);
                transmittance *= sampleTransmittance;
            }
            skip = false;
        }
    }
#endif

    finalTransmittance = transmittance;
    
    return L;
}

/*
 *
 */
float4 VolumetricMarch(float3 RayO, float3 RayDir, float rayOffset, out float finalAlpha) {
    const float3 RANDOM_VECTORS[6] = {float3( 0.38051305f,  0.92453449f, -0.02111345f),float3(-0.50625799f, -0.03590792f, -0.86163418f),float3(-0.32509218f, -0.94557439f,  0.01428793f),float3( 0.09026238f, -0.27376545f,  0.95755165f),float3( 0.28128598f,  0.42443639f, -0.86065785f),float3(-0.16852403f,  0.14748697f,  0.97460106f)};
	
    float Depth = 0.0;
    int NumMarches = 0;

    // Albedo (scatter/extinction ratio) is approximately 1
    // Extinction coefficient approximately the same as scattering
    //
    // stratus [0.04, 0.06]
    float3 ExtinctionCoefficient = cloudParams.extinction;//0.05; //float3(0.02, 0.05, 0.05);
    // cumulus [0.05, 0.12]
    // float3 ExtinctionCoefficient = float3(0.09, 0.09, 0.09);
    
    float4 Color = float4(0,0,0,0);

    float3 CurTransmittance = 1.0f;

    float Alpha = 0.0f;

    float numSamples = cloudParams.numSamples; // 256.;
    
    const float innerShellRadius = 6360 + INNER_SHELL_RADIUS; // in km
    const float outerShellRadius = 6360 + OUTER_SHELL_RADIUS; // in km
    
    const float distToInnerShellOg = GetNearestRaySphereDistance(RayO, RayDir, float3(0,0,0), innerShellRadius);
    const bool hitInnerShell = distToInnerShellOg > 0;

    float outerNearDist, outerFarDist;
    const bool hitOuterShell = GetRaySphereDistances(RayO, RayDir, float3(0,0,0), outerShellRadius, outerNearDist, outerFarDist);

    // dist between shells
    float raySampleLength;

    // ray hit both shells, then 
    if(hitInnerShell && hitOuterShell) {
        raySampleLength = abs(outerNearDist - distToInnerShellOg);
    }
    else if(hitOuterShell) {
        raySampleLength = 1. * abs(outerFarDist - outerNearDist);
    }
    // NOTE: ray cannot hit inner and not hit outer

    float dt;
    if(cloudParams.fixedDt) {
        dt = (OUTER_SHELL_RADIUS - INNER_SHELL_RADIUS) / numSamples; //;raySampleLength / numSamples;
    }
    else {
        dt = raySampleLength / numSamples; //5.2 * (OUTER_SHELL_RADIUS - INNER_SHELL_RADIUS) / numSamples; //;raySampleLength / numSamples;
    }
    
    //const float dt = raySampleLength / numSamples; //5.2 * (OUTER_SHELL_RADIUS - INNER_SHELL_RADIUS) / numSamples; //;raySampleLength / numSamples;
    //const float dt = (OUTER_SHELL_RADIUS - INNER_SHELL_RADIUS) / numSamples; //;raySampleLength / numSamples;
    const float largeDt = dt * cloudParams.largeDtScale;
    float t = 0.0;

    int numInBetween = 0;
    int totalPosDensity = 0;

    // 
    Depth = min(distToInnerShellOg, outerNearDist);
    float PrevDepth = Depth;
    
    float Rb = 6360;
    float distToGround = GetNearestRaySphereDistance(RayO, RayDir, float3(0,0,0), Rb);
    const bool hitGround = distToGround < 0;
    bool hitGroundFirst = distToGround > 0 && distToGround < distToInnerShellOg;
    bool skip = false;
    bool reachedEnd = false;
    
    bool noIntersection = !hitInnerShell && !hitOuterShell && !hitGround;

    /*
    finalAlpha = 1.;
    if(noIntersection) {
        return float3(1,0,0);
    }
    return float3(0,0,0);
    */
    

    PrevDepth = Depth;

    for(int i = 0; i < numSamples; i++) {
        const float3 Pos = RayO + RayDir * Depth;
        
        float StepSize = Depth - PrevDepth;
        PrevDepth = Depth;
        
        //const float distToInnerShell = GetNearestRaySphereDistance(Pos, RayDir, float3(0,0,0), innerShellRadius);
        // const float distToOuterShell = GetNearestRaySphereDistance(Pos, RayDir, float3(0,0,0), outerShellRadius);
        
        // radius of position
        const float r = length(Pos);

        numInBetween++;
            
        const float3 sampleOffset = cloudParams.windSpeed * renderContext.time * cloudParams.windDir;

        float lod2 = 0.9; //cloudParams.lodThresholds.x;
        float lod1 = 0.7; //cloudParams.lodThresholds.y;
        int lodMip = Depth > raySampleLength * lod2? 2 : Depth > raySampleLength * lod1? 1 : 0;
        const float Density = GetCloudDensityByPos(Pos, sampleOffset, true, lodMip);

        if(Density > 0.001) {

            if(!noIntersection && !hitGroundFirst) {
                totalPosDensity++;
            }
            
            if(NumMarches == 0.0) {
                NumMarches = 6;
                Depth -= rayOffset * lerp(dt, largeDt, 0.5);
                //skip = true;
            }

            NumMarches = min(6, NumMarches + 1);

            // Transmittance (camera to ray pos)
            float Tau0 = Density * 1. * StepSize;
            float3 PosTransmittance = exp(-Tau0);

            float3 LightToPos = normalize(float3(0.0, -0.8, -1.0));
            float3 ViewDir = normalize(-RayDir);
            float3 Phase = MiePhaseApproximation_HenyeyGreenstein(dot(LightToPos, ViewDir), cloudParams.phaseG);//SchlickPhaseFunction(dot(LightToPos, ViewDir), 0.2);

            float3 LightColor = cloudParams.lightColor;

            float Tau = 0.0f;
            int NumLightSamples = 6;
            float dxLight = dt * 0.333; // (1 - UVW.z) * (2 * CloudVolumeExtent.z) / NumLightSamples;
            
            float ncd = 0.0f;
            float cd = 0.0f;

            const float3 PosToLight = -LightToPos;

            float3 lightRayDensity = 0.0;
            
            for(int l = 0; l < NumLightSamples; l++) {
                //const float3 LightSamplePos = Pos + (PosToLight + RANDOM_VECTORS[l] * (float) l) * dxLight;
                const float3 LightSamplePos = Pos + PosToLight * dxLight;
                
                int mip = l > 3?  2  : lodMip;
                const float LightDensity = GetCloudDensityByPos(LightSamplePos, sampleOffset, l < 3, mip);
                lightRayDensity += LightDensity;
                Tau += LightDensity;
                
                cd += LightDensity;
                ncd += (LightDensity * (1.0-(cd*(1.0/(dxLight*6.)))));  
            }

            // distance sample
            /*
            const float3 LightSamplePos = (Pos) + (-LightToPos * 3.0 * dxLight);

            float LightDensity = GetCloudDensityByPos(LightSamplePos, sampleOffset, false, 2);
            Tau += LightDensity;

            cd += LightDensity;
            ncd += (LightDensity * (1.0-(cd*(1.0/(dxLight*6.)))));  

            Tau *= dxLight * ExtinctionCoefficient;

            */
            
            // float3 Visibility = exp(-Tau);
            // float Beers = exp(-Tau);
            // float3 Visibility = 2 * (1 - exp(-Tau * 2)) * (Beers);
            const float beersA = cloudParams.beersScale.x;
            const float beersB = cloudParams.beersScale.y;
            const float beersC = cloudParams.beersScale.z;
            const float beersD = cloudParams.beersScale.w;

            //cd = lightTransmittance;
            
            float ld = 2.8;
            float Beers = max(beersA * exp(-cd * dxLight), beersB * 0.7 * exp(-0.25 * cd * beersC * dxLight)); //max(exp(-dxLight * cd * ld), exp(-dxLight * cd * ld * 0.25) * 0.7);
            //float PowShug = 2. * (1.0 - exp(-dxLight * cd * ld * 2.0));
            float PowShug = 2. * (1.0 - exp(-cd * 2.0 * beersD * dxLight));
            float3 Visibility = PowShug * Beers;

            float3 lightRayAtt = exp(-1 * lightRayDensity * dxLight * cloudParams.extinction);

            lightRayAtt = PowShug * Beers;

            float r_alpha = GetHeightFraction(length(Pos));
            float3 Ambient = 0.0 * lerp(0.02, 1.0, (1 - r_alpha));
            
            float3 S = (Ambient + Phase * Visibility * LightColor) * ExtinctionCoefficient * Density;
            float3 SInt = (S - S * PosTransmittance) / (ExtinctionCoefficient * Density);
            // float3 L = SInt * CurTransmittance;
            //float3 L = cloudParams.lightColor.x * (Phase * Beers * PowShug * cloudParams.extinction);
            float3 L = cloudParams.lightColor.x * PowShug * Beers * Phase * cloudParams.extinction * CurTransmittance * Density * StepSize;

            if(!reachedEnd && !noIntersection && !hitGroundFirst ) {
                if(!skip) {
                    Color += float4(L, 0.0f);
                    Alpha += (1.0 - PosTransmittance.r) * (1.0 - Alpha);
                    //Alpha += Density * (1.0 - Alpha);
                    
                    //CurTransmittance = lerp(CurTransmittance, Density * Phase * Visibility, (1.0 - Density));
                }
                else {
                    skip = false;
                }
            }
            
            CurTransmittance *= PosTransmittance;
            
        }
        else {
            NumMarches = max(0, NumMarches - 1);
        }

        float step = dt;
        if(NumMarches <= 0) {
            //step = largeDt;
        }

        Depth += step + 0.5 * dt * rayOffset; //step * 0.5 * frac(Alpha * renderContext.time * RayDir * Depth); //max(0.0125, 0.02 * Depth);

        /*
        if(r > outerShellRadius) {
            Alpha = 1.;
            return float3(0,(float)numInBetween / numSamples,0);
        }
        */

        /*
        if(SDToVol <= 0.01) {
            Entered = true;
            SampleStartDepth = Depth;
            
            //if(length(Pos - CloudVolumeMin) <= 0.5) {
             //   Color += float4(0, 0.5, 0, 0);
            //}

            float3 PosOffset = 100 * RenderState.Time * float3(0.8, 0.03, -0.02);
            // float3 PosOffset = 0;
            
            float3 UVW = ((Pos - CloudVolumeMin) / (2 * CloudVolumeExtent));
            float3 UVW_Sample = ((Pos + PosOffset - CloudVolumeMin) / (2 * CloudVolumeExtent));

            Color = float4(UVW_Sample, 0.0);
            if(UVW_Sample.y > 0.99) {
               Color = float4(1,0,0, 0.0);
            }
            break;

            float Density = GetCloudDensity(UVW_Sample, true);

            if(Density > 0.001 && !Exited) {
                NumMarches++;

                // Transmittance (camera to ray pos)
                float Tau0 = Density * ExtinctionCoefficient * StepSize;
                //float3 PosTransmittance = 2 * exp(-Tau0) * (1 - exp(-Tau0 * 2));
                float3 PosTransmittance = exp(-Tau0);
                CurTransmittance *= PosTransmittance;

                // L Scatter(Pos, View Dir)
                float3 LightPos = float3(0, 0, 6000);

                // 1 light

                // Visibility = volShad(x, p_light) = T_r(x,p_light)
                float DistToLight = length(Pos - LightPos);
                
                float3 LightRadiance = float3(100,100,100) * (1 / (DistToLight * DistToLight) );
                // float3 LightRadiance = float3(1,1,1) * PI; 

                // float3 LightToPos = normalize(Pos - LightPos);
                // float3 PosToLight = normalize(LightPos - Pos);
                float3 LightToPos = normalize(float3(0.0, 0.0, -1.0));
                float3 ViewDir = normalize(-RayDir);
                float3 Phase = SchlickPhaseFunction(dot(LightToPos, ViewDir), 0.2);

                // float3 LightScatter = PI * Phase * Visibility * LightRadiance;
                // float3 FinalColor = PosTransmittance * LightScatter * ExtinctionCoefficient * StepSize;


                // Schneider
                float d = Density;
                float3 E = 2 * exp(-d) * (1 - exp(-2 * d)) * Phase;
                //

                // float c = PI * Phase * Visibility * LightRadiance * PosTransmittance * ExtinctionCoefficient;
                float3 LightColor = 60;

                float Tau = 0.0f;
                int NumLightSamples = 6;
                float dxLight = 30.0; // (1 - UVW.z) * (2 * CloudVolumeExtent.z) / NumLightSamples;
                
                float ncd = 0.0f;
                float cd = 0.0f;
                
                for(int l = 0; l < NumLightSamples; l++) {
                    float3 LightSamplePos = (Pos + PosOffset) + -LightToPos * l * dxLight;
                    float3 UVW_LightSample = (LightSamplePos - CloudVolumeMin) / (2 * CloudVolumeExtent);

                    float LightDensity = GetCloudDensity(UVW_LightSample, false);
                    Tau += LightDensity;

                    cd += LightDensity;
                    ncd += (LightDensity * (1.0-(cd*(1.0/(dxLight*6.)))));  
                }

                Tau *= dxLight * ExtinctionCoefficient;
                
                // float3 Visibility = exp(-Tau);
                // float Beers = exp(-Tau);
                // float3 Visibility = 2 * (1 - exp(-Tau * 2)) * (Beers);
                float ld = 0.4;
                float Beers = max(exp(-dxLight * ncd * ld), exp(-dxLight * ncd * ld * 0.25) * 0.7);
                float PowShug = 2. * (1.0 - exp(-dxLight * ncd * ld * 2.0));
                float3 Visibility = PowShug * Beers;
                
                float Ambient = 0.8 * lerp(0.15, 1.0, (1 - UVW.z));
                
                float c =
                            // Tr
                            CurTransmittance *

                            // L_scat (1 light)
                            //PI *
                            (
                                Ambient +
                                // PI *
                                Phase *
                                Visibility *
                                LightColor 
                            ) *

                            // Sigma_s as function of position, represented by density
                            ExtinctionCoefficient *
                            Density *
                                
                            // dt
                            StepSize;

                
                // float c = FinalColor;
                // float c = E;
                float3 Debug = float3(c,c,c);

                Color += float4(Debug, 0.0f);
                Alpha += (1.0 - PosTransmittance) * (1.0 - Alpha);
                
                Touched = true;
                float4 C = float4(lerp(float3(1,1,1), float3(0,0,0), BaseCloud), BaseCloud);
                C.a *= 0.6;
                C.rgb *= C.a;
                Color += C * (1.0 - Color.a);
            }

            float MarchStep = 10.0;
            float Max = 0.05;
            if(Density <= 0.001) {
                MarchStep = 30.0f;
                //Max = 0.2;
            }
            Depth += MarchStep + MarchStep * frac(52.98f * frac(rand3d(float3(0.06, 0.005, 0.09) * frac(RenderState.Time) * RayDir * Depth))); //max(0.0125, 0.02 * Depth);
        }
        else {
            if(Entered) {
                Exited = true;
            }
            
            if(SDToVol > 0) {
                Depth += abs(SDToVol);
            }
            else {
                Depth += 50.;
            }
        }
        */
    }

    if(totalPosDensity == 0 && !hitGroundFirst && !noIntersection) {
        // Color = float4(1,0,0,0);
    }

    // Color.rgb *= Alpha;
    Alpha = CurTransmittance.r;
    finalAlpha = hitGroundFirst || noIntersection? 0.0 : Alpha;

    float Gamma = cloudParams.lodThresholds.x; //2.2;
    float3 Mapped = float3(1,1,1) - exp(-Color.rgb * cloudParams.lodThresholds.y);
    Mapped = pow(Mapped, 1.0 / Gamma);

    return float4(Color.rgb, 1.0);
    //return float4(CurTransmittance, 1.0);
    // Mapped = pow(Mapped, 1.0 / Gamma);
    //const float cwhiteScale = 1.1575370919881305;
    //return U2Tone(Color.rgb) * cwhiteScale;
    // return Mapped;
}

float4 main(PSIn In, float4 screen_pos : SV_Position): SV_Target {
    const int bayerFilter[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    };

    int2 iscreen_pos = int2(screen_pos.xy);
    int index = renderContext.frame % 16;
    //int iCoord = (iscreen_pos.x + 4* iFragCoord.y) % BAYER_LIMIT;
    
    bool update = (((iscreen_pos.x + 4 * iscreen_pos.y) % 16)
            == bayerFilter[index]);

    if(!update) {
        // return float4(prevFrame.Sample(Sampler, screen_pos.xy / renderContext.screenSize).rgb, 0);
    }
    


    
    // dx12, screen_pos (0,0) is top left and (1,1) is bottom right.
    // However, we want (0,0) to be bottom left and (1,1) to be top right
    // We can do this by inverting y (ie remap using 1-y => y=1  ("bottom") is remapped to 0 ("top"), and vice versa)
    float ar = (float) renderContext.screenSize.x / (float) renderContext.screenSize.y;
    float2 uv = screen_pos.xy / renderContext.screenSize;

    float2 oguv = uv;
    uv.y = 1.0f - uv.y; // invert y
    
    uv.xy -= 0.5;
    
    uv.x *= ar;

    // float3 ro = float3(0,-23.,32.);
    float3 ro = float3(0,0,0.1) + float3(0,0,6360); // in km
    float3 fwd = normalize(float3(0., 1., 0.4325));
    float3 right = normalize(cross(fwd, float3(0,0,1)));
    float3 up = normalize(cross(right, fwd));
    
    const float3 ndc = float3(uv.x, uv.y, 1.0);
    const float4 viewPos = mul(renderContext.invProjectionMat, float4(ndc, 1.0));
    const float3 rayDir = normalize(mul((float3x3) renderContext.invViewMat, viewPos.xyz / viewPos.w));
    const float3 worldPos = renderContext.cameraPos + float3(0., 0., 6360.);

    float3x3 lookAt = float3x3(
        right.x, fwd.x, up.x,
        right.y, fwd.y, up.y,
        right.z, fwd.z, up.z
    );
    
    float3 rd = mul(lookAt, normalize(float3(uv.x, 1.0, uv.y)));



    float3 skyColor = 0.0;
    {
        const float r = length(worldPos);
        const float3 zenith = normalize(worldPos);
        const float cosTheta_Zv = dot(zenith, rayDir);

        const float3 sideDir = normalize(cross(zenith, rayDir));
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
    // rayOffset = frac(rayOffset + frac(renderContext.time) * 0.61803398875f);
    if(!cloudParams.useBlueNoise) {
        rayOffset = 0;
    }
    
    // const float3 CloudColor = VolumetricMarch(worldPos, rayDir, rayOffset, alpha);
    float3 transmittance;
    const float3 CloudColor = CloudMarch(worldPos, rayDir, rayOffset, skyColor, transmittance);
    alpha = transmittance.x;

    if(cloudParams.useAlpha == 0) {
        float a = alpha > 0? 1. : 0.;
        alpha = a;
    }

    float gamma = 2.2;
    float3 mapped = float3(1,1,1) - exp(-CloudColor * 3.0);
    mapped = pow(mapped, 1.0 / gamma);
    return float4(mapped, alpha);
}