struct PixelShaderInput 
{
    float4 color : COLOR;
    float4 clipTransform: CLIP_TRANSFORM; // (tX, tY, sX, sY)
    float2 uv : UV;
    float4 position : SV_Position;
};

Texture2D<float4> atlas : register(t0);
SamplerState atlasSampler : register(s0);

float median(float r, float g, float b) {
    return max(min(r,g), min(max(r, g), b));
}

float4 main(PixelShaderInput input) : SV_Target
{
    const float2 clipPos = input.clipTransform.xy;
    const float2 clipSize = input.clipTransform.zw;
    
    if(input.position.x < clipPos.x ||
       input.position.x > clipPos.x + clipSize.x) {
        discard;
    }
    
    //const float3 mask = atlas.SampleLevel(atlasSampler, input.uv, 0).rgb;
    //return float4(input.color.rgb * mask, input.color.a);

    float3 msd = atlas.SampleLevel(atlasSampler, input.uv, 0).rgb;
    float sd = median(msd.r, msd.g, msd.b);
    float screenPxDist = 4 * (sd - 0.5);
    float opacity = clamp(screenPxDist + 0.5, 0.0, 1.0);
    float3 black = float3(0,0,0);
    
    float3 color = lerp(black, input.color.rgb, opacity);
    
    return float4(input.color.rgb, input.color.a * opacity);
}

/*

// Original WebGL pixel shader from https://www.redblobgames.com/x/2403-distance-field-fonts/

uniform vec2 u_unit_range;
uniform float u_in_bias;
uniform float u_out_bias;
uniform float u_supersample;
uniform float u_smoothness;
uniform float u_gamma;

// some code from instructions on https://github.com/Chlumsky/msdfgen
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float screenPxRange() {
    vec2 screenTexSize =  vec2(1.0) / fwidth(v_texcoord);
    return max(0.5 * dot(u_unit_range, screenTexSize), 1.0);
}

float contour(float distance) {
    float width = screenPxRange();
    float e = width * (distance - 0.5 + u_in_bias) + 0.5 + u_out_bias;
    return  mix(clamp(e, 0.0, 1.0),
                smoothstep(0.0, 1.0, e),
                u_smoothness);
}

float sample(vec2 uv) {
    vec3 msd = texture2D(u_msdf_font, uv).rgb;
    float sd = median(msd.r, msd.g, msd.b);
    float opacity = contour(sd);
    return opacity;
}

void main() {
    float opacity = sample(v_texcoord);

    // from https://www.reddit.com/r/gamedev/comments/2879jd/comment/cicatot/
    float dscale = 0.354;
    vec2 uv = v_texcoord;
    vec2 duv = dscale * (dFdx(uv) + dFdy(uv));
    vec4 box = vec4(uv - duv, uv + duv);
    float asum = sample(box.xy)
               + sample(box.zw)
               + sample(box.xw)
               + sample(box.zy);
    opacity = mix(opacity, (opacity + 0.5 * asum) / 3.0, u_supersample);
    opacity = pow(opacity, 1.0/u_gamma);

    gl_FragColor = vec4(1, 1, 1, 1) * opacity;
}

*/
