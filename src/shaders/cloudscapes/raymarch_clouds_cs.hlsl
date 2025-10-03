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
    const float2 weatherUV = (samplePos.xy + float2(150, 0) + weatherRadius / 2.) / weatherRadius;
    
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
#define DEBUG_RETURN(v) finalTransmittance = 0.0f; return v;

float3 CloudMarch(float3 rayOrigin, float3 rayDir, float rayOffset, float3 skyColor, out float3 finalTransmittance) {
    const float3 RANDOM_VECTORS[6] = {float3( 0.38051305f,  0.92453449f, -0.02111345f),float3(-0.50625799f, -0.03590792f, -0.86163418f),float3(-0.32509218f, -0.94557439f,  0.01428793f),float3( 0.09026238f, -0.27376545f,  0.95755165f),float3( 0.28128598f,  0.42443639f, -0.86065785f),float3(-0.16852403f,  0.14748697f,  0.97460106f)};
    
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

        float uniformDt = stepSize; //lerp(start, end, alpha);
        
        float blueRand = blueNoise.SampleLevel(Sampler, cloudParams.beersScale.x * (rayOrigin + t * rayDir).xy, 0).r;
        //blueRand = frac(blueRand + frac(renderContext.time) * 0.61803398875f);
        if(!cloudParams.useBlueNoise) {
            blueRand = 0;
        }
        
        // uniformDt *= (1. / (min(2., numDensityPos + 1)));
        largeDt = largeDtBase; //largeDtBase * maxT; //pow(cloudParams.lodThresholds.y, numDensityZero); //clamp(numDensityZero, 0, largeDtThreshold));
        largeDt += largeDt * blueRand;

        const float dt = isSearching? largeDt: uniformDt + uniformDt * blueRand;
        t += dt;

        if(t > maxT) {
            reachedEnd = true;
        }

        if(transmittance.x < .01) {
            reachedEnd = true;
            transmittance = float3(0,0,0);
        }

        //const float newT = minT + raySampleLength * (i + rayOffset) / numSamples + dtScale;
        //const float dt = abs(newT - t);
        //t = newT;

        const float3 samplePos = rayOrigin + t * rayDir;

        if(length(samplePos) < Rb) {
            // reachedEnd = true;
        }
        
        const float3 sampleOffset = float3(0,0,0); // cloudParams.windSpeed * renderContext.time * cloudParams.windDir;

        const float4 b = cloudParams.beersScale;
        // int mip = t > b.x? 2 : t > b.y? 1 : 0;
        // int mip = isSearching? 1 : alpha > 0.5? 2 : 1;
        int mip = 0;
        const float density = GetCloudDensityByPos(samplePos, sampleOffset, !isSearching, mip);
        prevDensity = density;

        if(density <= 0.01) {
            numDensityZero++; //= max(largeDtThreshold, numDensityZero + 1);
        }

        if(density > 0) {
            
            if(isSearching) {
                skip = true;
                t -= dt;
                //largeDtThreshold = ogLargeDtThreshold * 0.5f;
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
            // float light_dt = cloudParams.beersScale.z * maxLightLen / numLightSamples;
            float light_dt = lightSampleLength / numLightSamples;

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

            const float lightLuminance = lerp(0.001, 1, lightAlpha);
            
            // const float3 curL = (ambient + skyColor * cloudParams.beersScale.w + lightLuminance * (lightTransmittance * miePhase) * scattering) * density;
            const float3 curL = (skyColor + lightLuminance * (lightTransmittance * miePhase) * scattering) * density;

            const float3 stMax = exp(-1. * (cloudParams.beersScale.w * extinction * dt));
            float3 st = sampleTransmittance; //max(sampleTransmittance, stMax);
            const float3 intS = (curL - curL * st);// / (density);

            if(!skip && !reachedEnd && radianceValid) {
                L += intS * transmittance;
                // L += lightColor;
                //float c = saturate(cos(dot(rayDir, lightDir)));
                //L += transmittance * lerp(RED, BLUE, c);
                transmittance *= sampleTransmittance;
            }
            skip = false;
        }
    }
#endif

    finalTransmittance = transmittance;
    //L = float3(1,1,1) * transmittance;
    
    return L;
}

float4 main(PSIn In, float4 screen_pos : SV_Position): SV_Target {
    const int bayerFilter[16] = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    };

    static const float4 colors[16] =
    {
        float4(1.0, 0.0, 0.0, 1.0),  // Red
        float4(0.0, 1.0, 0.0, 1.0),  // Green
        float4(0.0, 0.0, 1.0, 1.0),  // Blue
        float4(1.0, 1.0, 0.0, 1.0),  // Yellow
        float4(1.0, 0.0, 1.0, 1.0),  // Magenta
        float4(0.0, 1.0, 1.0, 1.0),  // Cyan
        float4(1.0, 0.5, 0.0, 1.0),  // Orange
        float4(0.6, 0.2, 0.8, 1.0),  // Purple
        float4(0.0, 0.5, 0.5, 1.0),  // Teal
        float4(0.5, 0.0, 0.5, 1.0),  // Dark Magenta
        float4(0.5, 0.5, 0.5, 1.0),  // Gray
        float4(0.3, 0.3, 1.0, 1.0),  // Light Blue
        float4(0.8, 0.2, 0.2, 1.0),  // Dark Red
        float4(0.2, 0.8, 0.2, 1.0),  // Dark Green
        float4(0.8, 0.8, 0.8, 1.0),  // Light Gray
        float4(0.2, 0.2, 0.2, 1.0)   // Dark Gray
    };

    int2 iscreen_pos = int2(screen_pos.xy);
    int index = renderContext.frame % 16;
    //int iCoord = (iscreen_pos.x + 4* iFragCoord.y) % BAYER_LIMIT;
    
    bool update = (((iscreen_pos.x + 4 * iscreen_pos.y) % 16)
            == bayerFilter[index]);

    float uvOffset = 0.;
    float2 uv = (screen_pos.xy + float2(uvOffset, uvOffset)) /
                renderContext.screenSize;
    
    if(!update) {
        float4 prevFrameVal = prevFrame.SampleLevel(prevFrameSampler, uv, 0);
        return float4(prevFrameVal.rgb, prevFrameVal.a);
    }

    // return float4(blueNoise.SampleLevel(prevFrameSampler, uv, 0).rgb, 0);
    
    // dx12, screen_pos (0,0) is top left and (1,1) is bottom right.
    // However, we want (0,0) to be bottom left and (1,1) to be top right
    // We can do this by inverting y (ie remap using 1-y => y=1  ("bottom") is remapped to 0 ("top"), and vice versa)
    float ar = (float) renderContext.screenSize.x / (float) renderContext.screenSize.y;

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
        float3 queryDir = float3(0,0,1);
        
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
    //rayOffset = frac(rayOffset + frac(renderContext.time) * 0.61803398875f);
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