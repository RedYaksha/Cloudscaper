#include "atmosphere_common.hlsl"
#include "common/render_common.hlsl"
#include "common/volumetric_rendering.hlsl"

struct PSIn {
    float2 UV : UV;
};

Texture2D<float4> skyViewLUT: register(t0);
SamplerState lutSampler : register(s0);

/*
struct RenderBuffer 
{
    float4x4 invProjectionMat;
    float4x4 invViewMat;
    uint2 screenSize;
    float2 pad0;
    float3 cameraPos;
    float pad1;
};
*/

ConstantBuffer<AtmosphereContext> atmosphere : register(b0);
ConstantBuffer<SkyBuffer> gSky : register(b1);
ConstantBuffer<RenderContext> renderContext : register(b2);

float4 main(PSIn In, float4 screen_pos : SV_Position): SV_Target {
    /*
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
        discard;
    }
    */
    
    // dx12, screen_pos (0,0) is top left and (1,1) is bottom right.
    // However, we want (0,0) to be bottom left and (1,1) to be top right
    // We can do this by inverting y (ie remap using 1-y => y=1  ("bottom") is remapped to 0 ("top"), and vice versa)
    const float ar = renderContext.screenSize.x / renderContext.screenSize.y;
    float2 uv = screen_pos.xy / renderContext.screenSize;
    float2 oguv = uv;
    uv.y = 1.0f - uv.y; // invert y
    
    uv.xy -= 0.5;
    uv.x *= ar;

    const float3 ndc = float3(uv.x, uv.y, 1.0);
    const float4 viewPos = mul(renderContext.invProjectionMat, float4(ndc, 1.0));
    const float3 rayDir = normalize(mul((float3x3) renderContext.invViewMat, viewPos.xyz / viewPos.w));
    const float3 worldPos = renderContext.cameraPos + float3(0., 0., atmosphere.Rb);

    // radius of worldPos
    const float r = length(worldPos);

    if(r <= atmosphere.Rt) {

        const float3 zenith = normalize(worldPos);
        const float cosTheta_Zv = dot(zenith, rayDir);

        const float3 sideDir = normalize(cross(zenith, rayDir));
        const float3 fwdDir = normalize(cross(sideDir, zenith));
        const float2 lightOnPlane = normalize(float2(dot(normalize(gSky.lightDir), fwdDir), dot(normalize(gSky.lightDir), sideDir)));
        const float cosTheta_lv = lightOnPlane.x;

        float2 skyView_uv;
        SkyViewParametersToUV(atmosphere, r, cosTheta_lv, cosTheta_Zv, skyView_uv);

        // sun disc
        float3 sunLuminance = 0.0;
        if (dot(rayDir, normalize(gSky.lightDir)) > cos(0.5*0.505*3.14159 / 180.0))
        {
            float t = GetNearestRaySphereDistance(worldPos, rayDir, float3(0.0f, 0.0f, 0.0f), atmosphere.Rb);
            if (t < 0.0f) // no intersection
            {
                //const float3 SunLuminance = 1000000.0; // arbitrary. But fine, not use when comparing the models
                sunLuminance = 10000000;
                //return SunLuminance * (1.0 - gScreenshotCaptureActive);
            }
        }
        
        // return float4(c,c,c,1);
        float3 L = skyViewLUT.SampleLevel(lutSampler, skyView_uv, 0).rgb + sunLuminance;

        // hdr / gamma
        float gamma = 2.2;
        float3 mapped = float3(1,1,1) - exp(-L * 3.0);
        mapped = pow(mapped, 1.0 / gamma);
        const float3 srgb = ConvertToSRGB(L);

        //return float4(1, 0, 0, 1.0);
        return float4(mapped, 1.0);
    }

    // float3 ro = float3(0,-23.,32.);
    float3 ro = float3(0,0,0);
    float3 fwd = normalize(float3(0., 1., 1.0));
    float3 right = normalize(cross(fwd, float3(0,0,1)));
    float3 up = normalize(cross(right, fwd));

    float3x3 lookAt = float3x3(
        right.x, fwd.x, up.x,
        right.y, fwd.y, up.y,
        right.z, fwd.z, up.z
    );
    
    float3 rd = mul(lookAt, normalize(float3(uv.x, 1.0, uv.y)));

    
    
    // return float4(c,c,c,1);
    float3 L = skyViewLUT.SampleLevel(lutSampler, oguv, 0).rgb;

    // hdr / gamma
    float gamma = 2.2;
    float3 mapped = float3(1,1,1) - exp(-L * 3.0);
    mapped = pow(mapped, 1.0 / gamma);

    return float4(1,0,0,1);
}