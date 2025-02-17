#define PI 3.1415926535897932384626433832795f

struct AtmosphereContext {
    float Rb; // Ground radius
    float Rt; // Atmosphere radius
};

// Represents the needed coefficients for rendering the atmosphere.
// All coefficients are dependent on the altitude (i.e distance of X above Rb) of the sample position.
struct MediumSample {
    float3 scattering;
    float3 absorption;
    float3 extinction;

    float3 scatteringMie;
    float3 absorptionMie;
    float3 extinctionMie;

    float3 scatteringRayleigh;
    float3 absorptionRayleigh;
    float3 extinctionRayleigh;

    float3 scatteringOzone;
    float3 absorptionOzone;
    float3 extinctionOzone;

    float3 albedo;
};

struct SkyBuffer {
    float3 cameraPos;
    float pad0;
    float3 lightDir;
    float pad1;
    float3 viewDir;
    float pad2;
    float3 sunIlluminance;
    float pad3;
    float3 groundAlbedo;
    float pad4;
};


// @param X - sample position
MediumSample GetMediumSample(in float3 X, in AtmosphereContext atmosphere) {
    // copied from Hillaire's implementation.
    // Side note, in the source code:
    //      12 floats are uploaded to the GPU and interpreted as 3 float4's.
    //      However, the data being stored is DensityProfileLayer, a struct of 5 floats.
    //      So in SkyAtmosphereCommon.hlsl, in GetAtmosphereParameters(), the data
    //      is extracted given that 2 DensityProfileLayers are spread amongst 3 float4's

    // The AbsorptionDensity variables reflect Ozone absorption in Hillaire's code.
    const float density0_layerWidth = 25.0f;
    const float density0_constantTerm = -2.0 / 3.0;
    const float density0_linearTerm = 1.0 / 15.0;
    const float density1_layerWidth  = 0.0;
    const float density1_constantTerm = 8.0 / 3.0;
    const float density1_linearTerm = -1.0 / 15.0;
    const float3 absorptionExtinction = float3(0.000650f, 0.001881f, 0.000085f);
    
    // altitude is how far X is above ground level
    const float alt = length(X) - atmosphere.Rb;

    const float densityRayleigh = exp(-alt / 8.0);
    const float densityMie = exp(-alt / 1.2);

    // TODO: need explanation, I suppose the ozone coefficients are modeled using
    // a simple linear function, but the coefficients to this function
    // are divided into 2 layers, depending on altitude
    const float densityOzone = saturate(alt < density0_layerWidth?
        density0_linearTerm * alt + density0_constantTerm :
        density1_linearTerm * alt + density1_constantTerm
    );
    
    MediumSample ret;

    // Rayleigh coefficients
    
    // SkyAtmosphereCommon.cpp -- SetupEarthAtmosphere()
    // NOTE: absorption = 0, thus rayExtinction = rayScattering
    const float3 rayScattering = float3(0.005802f, 0.013558f, 0.033100f);
    ret.scatteringRayleigh = densityRayleigh * rayScattering;
    ret.absorptionRayleigh = 0.0;
    ret.extinctionRayleigh = ret.scatteringRayleigh + ret.absorptionRayleigh;

    // Mie coefficients

    // SkyAtmosphereCommon.cpp -- SetupEarthAtmosphere()
    // NOTE: In Game.cpp, mie_absorption is uploaded just as [absorption = extinction - scattering]
    const float3 mieScattering = float3(0.003996f, 0.003996f, 0.003996f);
    const float3 mieExtinction = float3(0.004440f, 0.004440f, 0.004440f);
    const float3 mieAbsorption = mieExtinction - mieScattering;
    
    ret.scatteringMie = densityMie * mieScattering;
    ret.absorptionMie = densityMie * mieAbsorption;
    ret.extinctionMie = ret.scatteringMie + ret.absorptionMie;

    // Ozone coefficients
    ret.scatteringOzone = 0.0;
    ret.absorptionOzone = densityOzone * absorptionExtinction;
    ret.extinctionOzone = ret.scatteringOzone + ret.absorptionOzone;
    
    ret.scattering = ret.scatteringRayleigh + ret.scatteringMie + ret.scatteringOzone;
    ret.absorption = ret.absorptionRayleigh + ret.absorptionMie + ret.absorptionOzone;
    ret.extinction = ret.extinctionRayleigh + ret.extinctionMie + ret.extinctionOzone;
    
    ret.albedo = ret.scattering / max(0.001, ret.extinction); // avoid divide by zero
    
    return ret;
}

// IMPORTANT: This function assumes the ray does indeed intersect with the atmosphere boundary
//
// @param r - radius of sample point X (w.r.t planet center)
// @param mu - cos(angle between zenith and direction of interest, v)
//
// @return distance to the top of the atmosphere for ray(X, v)
float DistanceToTopAtmosphere(AtmosphereContext atmosphere, float r, float mu) {
    const float Rt = atmosphere.Rt;
    
    // The rationale for this equation is mostly explained in the
    // "Transmittance::Computation::Distance to the top atmosphere boundary"
    // section of https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html#transmittance_computation
    // We can solve for ||pi|| by completing the square.
    //
    // NOTE: if r > Rt, then d < 0
    const float d = sqrt(r * r * (mu * mu - 1) + Rt * Rt) - r * mu;
    return d;
}

// IMPORTANT: This function assumes the ray does indeed intersect with the atmosphere boundary
//
// @param r - radius of sample point X (w.r.t planet center)
// @param mu - cos(angle between zenith and direction of interest, v)
//
// @return distance to the bottom of the atmosphere for ray(X, v)
float DistanceToBottomAtmosphere(AtmosphereContext atmosphere, float r, float mu) {
    const float Rb = atmosphere.Rb;

    // The equation for DistanceToTopAtmosphere works exactly the same as finding
    // the distance to the bottom atmosphere.
    //
    // NOTE: if r < 
    const float d = sqrt(r * r * (mu * mu - 1) + Rb * Rb) - r * mu;
    return d;
}

// A general ray-sphere intersection test, as described in
// Real-Time Rendering (fourth edition), Akenine-Moller et al. Section 22.6.2 Intersection Test Methods :: Ray/Sphere Intersection :: Optimized Solution
//
// If intersections exist, all intersections (at most 2) are calculated but the nearest one is returned.
// Otherwise, -1.0 is returned.
//
// @param rayOrigin - origin of the ray
// @param rayDir - direction (normalized vector) of the ray
// @param sphereCenter - position of the center of the sphere
// @param sphereRadius - radius of the sphere
//
// @return
/*
float GetNearestRaySphereDistance(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float3 sphereRadius) {
    // renaming to match the book and useful squared values 
    const float3 c = sphereCenter;
    const float3 o = rayOrigin;
    const float3 d = rayDir;
    const float r = sphereRadius;
    const float r_squared = sphereRadius * sphereRadius;
    const float3 l = c - o;
    const float len_l = length(l);
    const float len_l_squared = len_l * len_l;

    // is rayOrigin is inside sphere => there's 1 intersection
    const bool originIsInsideSphere = len_l_squared < r_squared;

    // the projection of l onto d has a length of s
    const float s = dot(l, d);
    const float s_squared = s * s;
    
    // |l| and the projection of l onto d form a right triangle where,
    //      - |l| is the hypotenuse
    //      - s is the adjacent side
    //      - m is the opposite side
    const float m_squared = len_l_squared - s_squared;
    
    if(!originIsInsideSphere) {
        // (s < 0) => d points away from the sphere (specifically, angle between l and d > pi/2)
        //
        // is rayOrigin is outside the sphere and the ray is heading away from
        // it, then there is no intersection
        if(s < 0 && !originIsInsideSphere) {
            return -1.0;
        }
        
        // is m^2 > r^2 then the ray goes towards the sphere but misses it.
        if(m_squared > r_squared) {
            return -1.0;
        }
    }

    // the ray intersects the sphere
    // we have m^2 <= r^2

    // r and m form a right triangle where,
    //      - r is the hypotenuse
    //      - m is the side connected to the sphere center
    //      - q is the other side
    //
    const float q = sqrt(r_squared - m_squared);

    // q^2 = r^2 - m^2 has 2 solutions for q, thus we have 2 options:  
    const float t0 = s - q; 
    const float t1 = s + q; 

    // If rayOrigin is inside the sphere, (s - q) has no significant geometric meaning,
    // hence (s + q) is the (only) intersection length
    if(originIsInsideSphere) {
        return t1;
    }

    // (s - q) is closer to rayOrigin, hence it is the length of the nearest intersection point 
    return t0;

    // To evaluate the actual 3D intersection points, just plug them into the ray equation:
    //
    // Intersection 0 = I0 = rayOrigin + t0 * d;
    // Intersection 1 = I1 = rayOrigin + t1 * d;
}
*/

// 
// In the geometric model for the transmittance LUT:
//
// The context is: at any point, X, in the world, we'd like to calculate the transmittance
// to X, from an arbitrary direction v. More specifically, we'd like to shoot a ray
// from X to X+tv, and integrate the transmittance to see how much a value (e.g. irradiance)
// will falloff/decay as it travels towards X from the direction v. This ray
// can only travel so far until adding to transmittance has no effect - like when we hit the
// edge of the atmosphere, or if we hit the ground. The endpoint of this ray from X, we'll call it I.
//
// X is the sample position, X=(x,y,z)
//      - X intrinsically has an radius (distance from the planet's center, aka the origin),
//        r=|X| 
//
// v is the direction of interest. The direction we'd like to shoot a ray, from X.
//
// I is the "endpoint" of the query ray, in the direction of interest, v.
//
// Z is the "zenith" direction, which is just normalized X. This represent the "up" direction
// at X. It should make sense that Z is the direction of X from the origin.
//
// theta_Zv is the angle between the direction of interest and the zenith direction.
//
// mu is cos(theta_zv). When Z and v align, mu = 1, and when they are pointing in opposite directions, mu = -1
//
// We can construct all variables here just from X and v, just 2 variables. We also have 2 variables available for use: u and v.
// The only problem is that u and v can only be in the range [0,1] and X and v are both 3D vectors. However, we can
// still store them by:
//
// u will 'somehow' store v by using mu.
// v will 'somehow' store X by using its altitude, represented by its percentage, a value in [0,1], between R_g and R_t.
//
// The mappings from (u,v) to (X,v) and vice-versa is done non-linearly to account for the spherical shape of the planet.
// The nonlinear mapping is derived from elementary geometry - however the intuition behind it is non-trivial, at least for me.
//

// @param atmosphereParams - constant atmosphere context
// @param uv - float2 from [0,1]^2
//
// @out r - radius of X
// @out mu - cos(theta_Zv), the cosine of the angle between the zenith and the direction of interest
void UVToLightTransmittanceParameters(AtmosphereContext atmosphere, float2 uv, out float r, out float mu) {
    const float u = uv.x;
    const float v = uv.y;
    
    const float Rt = atmosphere.Rt;
    const float Rb = atmosphere.Rb;

    // H is the horizon length. Formed by a right triangle where Rb and H
    // are the sides and Rt is the hypotenuse.
    const float H = sqrt(Rt * Rt - Rb * Rb);
    
    // --- v stores altitude by using its percentage of H ---
    //
    // rho is how much H will extend in the other direction.
    const float rho = H * v;

    // Calculation of r
    //
    // The endpoint of the H+rho line is our X, whose radius, r
    // can be calculated by the triangle where rho and Rb are sides
    // and r is the hypotenuse.
    r = sqrt(rho * rho + Rb * Rb);

    // Calculation of mu
    //
    // d is the length of a line extending to the edge of the atmosphere from x.
    // --- d is interpolated between d_min, and d_max, where the alpha used is u. ---
    // 
    // The calculation of mu comes from the observation that |I| = Rt. And using
    // the coordinates of I (which can be derived from X and mu) we can deduce the length of XI=d.
    // When setting |I|=Rt, and using elementary algebra (completing the square!), we can conclude that
    // mu = (Rt^2 - d^2 - r^2) / (2rd)
    const float d_min = Rt - r;
    const float d_max = rho + H;
    const float d = d_min + u * (d_max - d_min);

    //mu = (Rt * Rt - d * d - r * r) / (2 * r * d);
    mu = (H * H - rho * rho - d * d) / (2 * r * d);

    // Interestingly, this is equivalent to:
    // mu = (H * H - rho * rho - d * d) / (2rd)
    // which is what's actually used in Bruneton's/Neyrat's implementation,
    // https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html#transmittance_precomputation
    //
    // This equivalence implies that Rt^2 - r^2 = H^2 - rho^2, which is obvious algebraically, but
    // not geometrically (to me). Maybe someone can shed some light into this part of the calculation.
    //
    // I'm unsure why the 2nd variation is used when the first one naturally falls out of the algebra.
    //
    // I also initially believed the derivation of mu came directly from the Law of Cosines, because of
    // the factor of 2rd. But the formulation never matched up when looking at the Bruneton's calculation of mu. But the
    // 2rd is curious, and perhaps the Law of Cosines is somewhere in the geometry, but I just can't figure it out.
}

// @param - radius of X (which will get transformed to represent altitude by u)
void LightTransmittanceParametersToUV(AtmosphereContext atmosphere, float r, float mu, out float2 uv) {
    const float Rt = atmosphere.Rt;
    const float Rb = atmosphere.Rb;
    
    // H is the horizon length. Formed by a right triangle where Rb and H
    // are the sides and Rt is the hypotenuse.
    const float H = sqrt(Rt * Rt - Rb * Rb);

    // 
    const float rho = sqrt(r * r - Rb * Rb);

    const float d = sqrt(r * r * (mu * mu - 1) + Rt * Rt) - r * mu;
    const float d_min = Rt - r;
    const float d_max = rho + H;

    const float u = (d - d_min) / (d_max - d_min);
    const float v = rho / H;

    uv.x = u;
    uv.y = v;
}

//
// MultiScattering Parameterization
//
// Context: When computing the integral in order to calculate volumetric irradiance by ray marching, at
// each sample point along the ray, X, there is an addition to the illuminance from scattering. When computing the integral,
// the following should be readily available (since we're ray-marching wrt a light source):
//
// X - the sample point
// r - radius, computed by |X|
// Z - zenith direction, computed by normalizing X
// l - light direction (e.g. the sun)
//
// Since these variables are available, we can store the multi-scattering contribution in a LUT according to the
// following mapping:
//
// The mapping between (u,v) and (cos(theta_Zl), r), as explained in section 5.5.2 (Hillaire):
//    - u = 0.5 + 0.5 * cos(theta_zl) <==> cos(theta_zl) = 2 * u - 1
//    - v = max(0, min((r-Rb)/(Rt-Rb), 1)) <==> r = Rb + v * (Rt-Rb)
//
// v <--> r
//    - In the mapping of r -> v, v is ensured to be in the range [0,1], and in v -> r, it is assumed v is [0,1]
//    - v is the percentage of r in between Rg and Rt
//
// u <--> cos(theta_Zl)
//    - if u = 0 => cos(theta_Zl) = -1 => theta_Zl = pi => light and zenith are pointing in opposite directions 
//    - if u = 1 => cos(theta_Zl) = 1 => theta_Zl = 0 => light and zenith are aligned (sun is directly above X)
//    - This is a linear mapping, so light-view alignment get equal representation in this LUT
// 

// @param atmosphere - constant atmosphere context
// @param uv  - float2 in [0,1]^2
//
// @out cosTheta_Zl - cos(angle between zenith direction and the light)
// @out r - radius of X
void UVToMultiScatteringParameters(AtmosphereContext atmosphere, float2 uv, out float cosTheta_Zl, out float r) {

    const float u = uv.x;
    const float v = uv.y;

    const float Rb = atmosphere.Rb;
    const float Rt = atmosphere.Rt;

    cosTheta_Zl = 2 * u - 1;
    r = v * (Rt - Rb) + Rb;
}

//
void MultiScatteringParametersToUV(AtmosphereContext atmosphere, float cosTheta_Zl, float r, out float2 uv) {
    const float Rb = atmosphere.Rb;
    const float Rt = atmosphere.Rt;
    
    const float u = 0.5 + 0.5 * cosTheta_Zl;
    const float v = clamp((r - Rb) / (Rt - Rb), 0., 1.);

    uv.x = u;
    uv.y = v;
}

//
// SkyView Parameterization
//
// Context: For fast rendering of the atmosphere, where "volumetric shadow [is] disabled", we store the
// illuminance in a latitude/longitude texture. I.e., from the position of the camera, X, we're going to store the
// illuminance all around X. Plainly, we're going to shoot rays from X at all different angles, forming a sphere - mapping
// the direction of the ray to texels in a 2D texture, and storing the integrated illuminance, wrt the ray, at each texel. Since
// "more visual features are visible towards the horizon" (Hillaire), the directions of these rays are biased towards the horizon.
// Essentially we will be assigning more texels towards the horizon, since most of the detail are in that area. We do this
// with the following mapping:
//
// u = sqrt(0.5 * (1 - cos(theta_lv)) <==> cos(theta_lv) = -(2 * u^2 - 1)
// v = [explained below]
//
// u <--> cos(theta_lv)
//      - u is mapped to a value [-1,1] representing the cosine of the angle between the light and direction of interest.
//      - If u=0, cos(theta_lv) = 1, implying that l and v are aligned
//      - If u=1, cos(theta_lv) = -1, implying that l and v point in opposite directions
//      - The nonlinear relationship between u and cos(theta_lv) ensures that more u texels are assigned to cos(theta_lv) > 0,
//        implying the SkyView LUT will have more detail for queries near the light source, which makes sense as that is the
//        area with most detail
//
// v <--> cos(theta_Zv)
//      - v is mapped to a value [-1,1] representing the cosine of the angle between the zenith direction and direction of interest, v.
//      - The goal in this mapping is to map:
//              Goal 1:: v=0 => cos(theta_Zv) = 1 => v and zenith align, so this will be used for queries directly above X (i.e. away center of planet)
//              Goal 2:: v=0.5 => cos(theta_Zv) = cos(theta_Zh) => v aligns with the horizon, 
//              Goal 3:: v=1 => cos(theta_Zv) = -1 => v and zenith are facing opposite of each other, used for queries directly below X (i.e. towards center of planet)
//              Goal 4:: As v approaches 0.5 from both directions, cos(theta_Zv) should approach cos(theta_Zh) rapidly and stick around, to assign more texels towards the horizon.
//                       More technically, the derivative of this mapping when v=0.5, should be 0. A simple quadratic curve
//                       is perfect for this scenario, as mentioned in the paper (Hillaire).
//      - As the horizon could be at an arbitrary angle from the zenith, but we still want to map v=0.5 to it, this will require
//        as nonlinear mapping, and further will require us to map v=[0,0.5) and v=[0.5,1] differently from each other.
//      - Aside:
//              - Treating each range differently isn't completely necessary but it will be easier to create a nonlinear function
//                this way.
//              - An alternative would be to create a cubic spline function that is flat around where v maps to the horizon,
//                which will accomplish the same goal (of assigning more texels towards the horizon)
//      - When v < 0.5:
//              - The mapping from v=[0,0.5) to cos(theta_Zv) will correspond to when ...v and zenith align...
//                to when ...v and horizon align...
//              - So the goal is to have a nonlinear mapping from v=[0,0.5) to values of to theta_Zv=[0,theta_Zh),
//                where the mapping is "flatter" towards v=0.5 (for assigning more texels towards the horizon)
//              - To achieve this, we can interpolate theta_Zh using a nonlinear function, f, that is flat when v=0.5,
//                in other words we can calculate cosTheta_Zv = cos(theta_Zh * f(v))
//              - By goal 1, when v=0, we want f(v)=0, to ensure cos(theta_Zv) = 1
//              - By goal 2, when v=0.5, we want f(v)=1, to ensure cos(theta_Zv) = cos(theta_Zh)
//              - By goal 4, f'(0.5) = 0
//              - To achieve all goals we can define f to be a simple quadratic: f(v) = 1 - (1 - 2v)^2
//                      - f(0) = 1 - (1)^2 = 0
//                      - f(0.5) = 1 - (1 - 2/2)^2 = 1
//                      - f'(0.5) = 4 - 8(0.5) = 0
//              - Then, as needed,
//                      - v=0 => cos(theta_Zv) = cos(theta_Zh * f(0)) = 1
//                      - v=0.5 => cos(theta_Zv) = cos(theta_Zh * f(0.5)) = cos(theta_Zh)
//              
//      - When v >= 0.5:
//              - The mapping from v=[0.5, 1] to cos(theta_Zv) will correspond to when ...v and horizon align... to
//                when ...v is opposite of zenith...
//              - In this case, we want a nonlinear mapping from v=[0.5, 1] to values theta_Zv=[theta_Zh, PI],
//                where the mapping has derivative 0 when v=0.5
//              - Unlike before, where we are interpolating theta_Zv from 0 to theta_Zh, we can interpolate from 0 to (PI - Zh),
//                and add that to theta_Zh, which will give us the mapping range we we need. I.e. we will calculate:
//                cos(theta_Zv) = cos(theta_Zh + f(v) * (PI - Zh))
//              - By goal 2, when v=0.5, we want f(v)=0, to ensure cos(theta_Zv) = cos(theta_Zh)
//              - By goal 3, when v=1, we want f(v)=1, to ensure cos(theta_Zv) = cos(PI)
//              - By goal 4, f'(0.5) = 0
//              - If you notice carefully, we want f(v) to range (0,1), like above, just for different values of v.
//              - To achieve all these goals we can define f to be a 'different' simple quadratic: f(v) = (2v - 1)^2
//                      - f(0.5) = (2/2 - 1)^2 = 0
//                      - f(1) = (2*1 - 1)^2 = 1 
//                      - f'(0.5) = = 8(0.5) - 4 = 0
//              - Then, as needed,
//                      - v=0.5 => cos(theta_Zv) = cos(theta_Zh + (PI - theta_Zh) * f(0.5)) = cos(theta_Zh)
//                      - v=1 => cos(theta_Zv) = cos(theta_Zh + (PI - theta_Zh) * f(1)) = cos(PI)
//

//
// @param atmosphere - constant atmosphere context
// @param uv - float2 in [0,1]^2
// @out cosTheta_lv - 
// @out cosTheta_Zv -
void UVToSkyViewParameters(AtmosphereContext atmosphere, float2 uv, float r, out float cosTheta_lv, out float cosTheta_Zv) {
    const float u = uv.x;
    const float v = uv.y;

    const float Rb = atmosphere.Rb;

    // distance from r to horizon
    // Formed by the right triangle where r is the hypotenuse and
    // d_horizon and Rb are the side lengths. This so happens to be the
    // same distance as rho in the transmittance parameterization calculations.
    const float d_horizon = sqrt(r * r - Rb * Rb);
    
    const float cosBeta = d_horizon / r;
    const float beta = acos(cosBeta);
    const float theta_Zh = PI - beta;

    if(v < 0.5) {
        const float CA = 1 - (1 - 2 * v) * (1 - 2 * v);
        cosTheta_Zv = cos(theta_Zh * CA);
    }
    else { // v >= 0.5
        const float CB = (2 * v - 1) * (2 * v - 1);
        cosTheta_Zv = cos(theta_Zh + beta * CB);
    }

    // This allocates more texels for cosTheta_lv to be greater than 0 => more detail for directions aligned to the
    // light source (e.g. the sun)
    cosTheta_lv = -(2.0 * u * u - 1.0);
}

//
void SkyViewParametersToUV(AtmosphereContext atmosphere, float r, float cosTheta_lv, float cosTheta_Zv, out float2 uv) {
    const float Rb = atmosphere.Rb;
    
    const float d_horizon = sqrt(r * r - Rb * Rb);
    
    const float cosBeta = d_horizon / r;
    const float beta = acos(cosBeta);
    const float theta_Zh = PI - beta;
    
    const float theta_Zv = acos(cosTheta_Zv);

    float v;

    // if theta_Zv is under the horizon (ie if cos(theta_Zv) is more negative than cos(theta_Zh))
    // then we use the inverse of the mapping we use when v < 0.5
    //
    // The inverse is calculated directly from the formulation of cosTheta_Zv, but solving for v
    if(cosTheta_Zv > cos(theta_Zh)) {
        v = 0.5 * (1 - sqrt(1 - (theta_Zv / theta_Zh)));
    }
    else {
        v = 0.5 * (sqrt((theta_Zv - theta_Zh) / beta) + 1);
    }

    const float u = sqrt((1 - cosTheta_lv) / 2);

    uv.x = u;
    uv.y = v;
}

//
// Phase functions
//

// The Rayleigh phase function.
// This is defined in section 4 (Hillaire), but also in Real-Time Rendering (4th ed.) pg.597
/*
float RayleighPhase(float cosTheta) {
    return (3 * (1 + cosTheta * cosTheta)) / (16 * PI);
}
*/

// The Cornette-Shanks approximation of the Mie phase function
// This is defined in sestion 4 (Hillaire)
/*
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

// The Schlick approximation to the Henyey-Greenstein phase function, a cheaper alternative.
// The definition can be found in Real-Time Rendering (4th Ed.) pg. 599
float MiePhaseApproxmation_HenyeyGreensteinSchlick(float cosTheta) {
    const float g = 0.8;

    const float k = 1.55 * g - 0.55 * g * g * g;

    // squared part of the denominator
    float denom_sq = 1 + k * cosTheta;
    denom_sq *= denom_sq;

    return (1 - k * k) / ((4 * PI) * denom_sq);
}
*/

float3 GetLightTransmittance(Texture2D<float4> inLUT, SamplerState inSampler, AtmosphereContext atmosphere, float r, float cosTheta_Zv) {
    float2 uv;
    LightTransmittanceParametersToUV(atmosphere, r, cosTheta_Zv, uv);

    // if atmosphere hits the ground, return 0...?

    return inLUT.SampleLevel(inSampler, uv, 0).rgb;
}

float3 GetMultiScattering(Texture2D<float4> inLUT, SamplerState inSampler, AtmosphereContext atmosphere, float cosTheta_Zl, float r) {
    float2 uv;
    MultiScatteringParametersToUV(atmosphere, cosTheta_Zl, r, uv);

    return inLUT.SampleLevel(inSampler, uv, 0).rgb;
}

