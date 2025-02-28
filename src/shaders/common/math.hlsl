#ifndef GAME_COMMON_MATH_HLSL_
#define GAME_COMMON_MATH_HLSL_

#define PI 3.1415926535897932384626433832795f

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
float GetNearestRaySphereDistance(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float sphereRadius) {
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

uint GetRaySphereDistances(float3 rayOrigin, float3 rayDir, float3 sphereCenter, float sphereRadius, out float nearDist, out float farDist) {
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
            return 0;
        }
        
        // is m^2 > r^2 then the ray goes towards the sphere but misses it.
        if(m_squared > r_squared) {
            return 0;
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
        nearDist = t1;
        farDist = -1;
        return 1;
    }

    // (s - q) is closer to rayOrigin, hence it is the length of the nearest intersection point
    nearDist = t0;
    farDist = t1;
    return 2;

    // To evaluate the actual 3D intersection points, just plug them into the ray equation:
    //
    // Intersection 0 = I0 = rayOrigin + t0 * d;
    // Intersection 1 = I1 = rayOrigin + t1 * d;
}

#endif // GAME_COMMON_MATH_HLSL_
