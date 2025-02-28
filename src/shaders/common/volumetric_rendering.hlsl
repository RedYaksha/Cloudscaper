#ifndef GAME_COMMON_VOLUMETRIC_RENDERING_HLSL_
#define GAME_COMMON_VOLUMETRIC_RENDERING_HLSL_

#include "math.hlsl"

// The Rayleigh phase function.
// This is defined in section 4 (Hillaire), but also in Real-Time Rendering (4th ed.) pg.597
float RayleighPhase(float cosTheta) {
    return (3 * (1 + cosTheta * cosTheta)) / (16 * PI);
}

// The Cornette-Shanks approximation of the Mie phase function
// This is defined in sestion 4 (Hillaire)
float MiePhaseApproximation_CornetteShanks(float cosTheta) {
    const float g = 0.8;
    const float g_sq = g * g;

    const float cosT_sq = cosTheta * cosTheta;

    return
        (3 / (8 * PI)) * // scalar
        ((1 - g_sq) * (1 + cosT_sq)) // numerator
        /
            // -cosTheta isn't in paper, but is in code...
        ((2 + g_sq) * pow((1 + g_sq - 2 * g * -cosTheta), 3./2.)) // denominator
    ;
}

// The Henyey-Greenstein approximation of the Mie phase function
// The definition can be found in Real-Time Rendering (4th Ed.) pg. 598
float MiePhaseApproximation_HenyeyGreenstein(float cosTheta) {
    const float g = 0.8;
    const float g_sq = g * g;
    return (1 - g_sq) / ((4 * PI) * pow(1 + g_sq - 2 * g * cosTheta, 1.5));
}

float MiePhaseApproximation_HenyeyGreenstein(float cosTheta, float g) {
    const float g_sq = g * g;
    return (1 - g_sq) / ((4 * PI) * pow(1 + g_sq - 2 * g * cosTheta, 1.5));
}

// The Schlick approximation to the Henyey-Greenstein phase function, a cheaper alternative.
// The definition can be found in Real-Time Rendering (4th Ed.) pg. 599
float MiePhaseApproximation_HenyeyGreensteinSchlick(float cosTheta) {
    const float g = 0.8;

    const float k = 1.55 * g - 0.55 * g * g * g;

    // squared part of the denominator
    float denom_sq = 1 + k * cosTheta;
    denom_sq *= denom_sq;

    return (1 - k * k) / ((4 * PI) * denom_sq);
}

float MiePhaseApproximation_HenyeyGreensteinSchlick(float cosTheta, float g) {
    const float k = 1.55 * g - 0.55 * g * g * g;

    // squared part of the denominator
    float denom_sq = 1 + k * cosTheta;
    denom_sq *= denom_sq;

    return (1 - k * k) / ((4 * PI) * denom_sq);
}

#endif //GAME_COMMON_VOLUMETRIC_RENDERING_HLSL_
