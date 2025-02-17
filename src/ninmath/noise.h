#ifndef NINMATH_NOISE_H_
#define NINMATH_NOISE_H_
#define NOMINMAX
#include <algorithm>
#include "ninmath.h"

namespace ninmath {
namespace noise {

    // https://www.shadertoy.com/view/slB3z3
    // murmur hash
    inline uint32_t MurmurHash3D(Vector3f x, uint32_t seed) {
        const uint32_t m = 0x5bd1e995U;
        uint32_t hash = seed;

        uint32_t k = (uint32_t) x.x;
        k *= m;
        k ^= k >> 24;
        k *= m;
        hash *= m;
        hash ^= k;
        
        k = (uint32_t) x.y;
        k *= m;
        k ^= k >> 24;
        k *= m;
        hash *= m;
        hash ^= k;
        
        k = (uint32_t) x.z;
        k *= m;
        k ^= k >> 24;
        k *= m;
        hash *= m;
        hash ^= k;

        hash ^= hash >> 13;
        hash *= m;
        hash ^= hash >> 15;

        return hash;
    }

    inline Vector3f GradientDirection(uint32_t hash) {
        switch (int(hash) & 15) { // look at the last four bits to pick a gradient direction
        case 0:
            return Vector3f(1, 1, 0);
        case 1:
            return Vector3f(-1, 1, 0);
        case 2:
            return Vector3f(1, -1, 0);
        case 3:
            return Vector3f(-1, -1, 0);
        case 4:
            return Vector3f(1, 0, 1);
        case 5:
            return Vector3f(-1, 0, 1);
        case 6:
            return Vector3f(1, 0, -1);
        case 7:
            return Vector3f(-1, 0, -1);
        case 8:
            return Vector3f(0, 1, 1);
        case 9:
            return Vector3f(0, -1, 1);
        case 10:
            return Vector3f(0, 1, -1);
        case 11:
            return Vector3f(0, -1, -1);
        case 12:
            return Vector3f(1, 1, 0);
        case 13:
            return Vector3f(-1, 1, 0);
        case 14:
            return Vector3f(0, -1, 1);
        case 15:
            return Vector3f(0, -1, -1);
        default:
            return Vector3f();
        }
    }
    
    inline float Perlin(Vector3f p) {
        const Vector3f int_part = Floor(p);
        const Vector3f fract_part = Fract(p);
            
        uint32_t seed = 0x578437adU;
            
        // generate cube grid points
        const Vector3f g0 = int_part; // bl
        const Vector3f g1 = int_part + Vector3f(1.f, 0.f, 0.f); // br
        const Vector3f g2 = int_part + Vector3f(0.f, 1.f, 0.f); // tl 
        const Vector3f g3 = int_part + Vector3f(1.f, 1.f, 0.f); // tr 
        
        const Vector3f g4 = int_part + Vector3f(0.f, 0.f, 1.f); // br
        const Vector3f g5 = int_part + Vector3f(1.f, 0.f, 1.f); // tl 
        const Vector3f g6 = int_part + Vector3f(0.f, 1.f, 1.f); // tr 
        const Vector3f g7 = int_part + Vector3f(1.f, 1.f, 1.f); // tr

        // generate dot product between grid point unit vectors and (p - grid point)
        const float d0 = GradientDirection(MurmurHash3D(g0, seed)).Dot(p - g0);
        const float d1 = GradientDirection(MurmurHash3D(g1, seed)).Dot(p - g1);
        const float d2 = GradientDirection(MurmurHash3D(g2, seed)).Dot(p - g2);
        const float d3 = GradientDirection(MurmurHash3D(g3, seed)).Dot(p - g3);
        const float d4 = GradientDirection(MurmurHash3D(g4, seed)).Dot(p - g4);
        const float d5 = GradientDirection(MurmurHash3D(g5, seed)).Dot(p - g5);
        const float d6 = GradientDirection(MurmurHash3D(g6, seed)).Dot(p - g6);
        const float d7 = GradientDirection(MurmurHash3D(g7, seed)).Dot(p - g7);

        // use fract part of position to interpolate between the dot products.
        // the fract part is transformed by a smoothing function first
        //
        // smoothing function
        //      6t^5 - 15t^4 + 10t^3
        const Vector3f t = fract_part;
        const Vector3f u = t * t * t * ( t * (t * 6.0f - 15.f) + 10.f);
        
        // mix
        const float M0 = Lerp(d0, d1, u.x);
        const float M1 = Lerp(d2, d3, u.x);
        const float M2 = Lerp(d4, d5, u.x);
        const float M4 = Lerp(d6, d7, u.x);

        const float M5 = Lerp(M0, M1, u.y);
        const float M6 = Lerp(M2, M4, u.y);

        const float M7 = Lerp(M5, M6, u.z);

        return M7;
    }

    inline Vector3f Hash33(Vector3f p) {
        Vector3f P = Fract(p * Vector3f(0.1031f, 0.11369f, 0.13787f));
        Vector3f P_yxz = {p.y, p.x, p.z};
        P = P + P.Dot(P_yxz + 19.19f);
        return -1.0f + 2.0f *
            Fract(Vector3f(
                (P.x + P.y)*P.z,
                (P.x + P.z)*P.y,
                (P.y + P.z)*P.x
            ));
    }

    inline float Worley(Vector3f p, float scale) {
        // based on "repeatable 3d worley noise" by hong1991
        // https://www.shadertoy.com/view/3d3fWN
        
        Vector3f grid_point = Floor(p * scale); // center grid point
        Vector3f fract_part = Fract(p * scale);

        float min_dist = FLT_MAX; // std::numeric_limits<float>::max();

        // 
        for(int x = -1; x <= 1; x++) {
            for(int y = -1; y <= 1; y++) {
                for(int z = -1; z <= 1; z++) {
                    Vector3f offset((float)x,(float)y,(float)z);
                    Vector3f cur_grid_point = grid_point + offset;
                    Vector3f rId = Hash33(Mod(cur_grid_point, scale)) * 0.5f + 0.5f;

                    // Vector3f r = (grid_point + fract_part) - (grid_point + offset) - rId;
                    Vector3f r = offset + rId - fract_part;

                    float d = r.Dot(r);

                    if(d < min_dist) {
                        min_dist = d;
                    }
                }
            }
        }

        return min_dist;
    }
    
    inline float PerlinFBM(Vector3f p) {
        float gain = 0.5f;
        float lacunarity = 2.f;
        int octaves = 3;

        float amplitude = 0.5;
        float freq = 8.f;
        
        float ampSum = 0.f;

        float val = 0.f;
        for(int i = 0; i < octaves; i++) {
            val += amplitude * Perlin(p * freq);
            freq *= lacunarity;
            
            ampSum += amplitude;
            amplitude *= amplitude;
        }
        return std::clamp((val / ampSum) * 0.5f + 0.5f, 0.0f, 1.0f);
    }
}
} // namespace ninmath
#endif // NINMATH_NOISE_H_
