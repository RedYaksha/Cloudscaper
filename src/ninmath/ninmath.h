#pragma once
#define NOMINMAX
#include <cmath>
#include <numbers>

namespace ninmath {

typedef struct Vector2f {
    Vector2f() : x(0), y(0) {}
    
    Vector2f(float x, float y) 
	: x(x), y(y) {}

    static Vector2f Zero() {
        return Vector2f {0.f,0.f};
    }
    
    float x;
    float y;
} Vector2f;


typedef struct Vector3f {
    Vector3f() : x(0), y(0), z(0) {}
    
    Vector3f(float x, float y, float z) 
	: x(x), y(y), z(z) {}

    float x;
    float y;
    float z;

    Vector3f Cross(const Vector3f& Other) const {
        const Vector3f& A = *this;
        const Vector3f& B = Other;
        return {A.y * B.z - A.z * B.y,
                A.z * B.x - A.x * B.z,
                A.x * B.y - A.y * B.x
                };
    }

    float Dot(const Vector3f& Other) const {
        const Vector3f& A = *this;
        const Vector3f& B = Other;
        return A.x * B.x + A.y * B.y + A.z * B.z;
    }

    Vector3f Normal() const {
        const float Len = Length();
        return { x / Len, y / Len, z / Len };
    }

    float Length() const {
        return std::sqrt(x*x + y*y + z*z);
    }

} Vector3f;

typedef struct Vector4f {
    Vector4f() : x(0), y(0), z(0), w(0) {}
    
    Vector4f(float x, float y, float z, float w) 
	: x(x), y(y), z(z), w(w) {}
    
    float Dot(const Vector4f& Other) const {
        const Vector4f& A = *this;
        const Vector4f& B = Other;
        return A.x * B.x + A.y * B.y + A.z * B.z + A.w * B.w;
    }

    union {
        struct {
            float x, y, z, w;
        };
        struct {
            float l, r, t, b;
        };
    };
} Vector4f;
    
typedef struct Vector2i {
    Vector2i() : x(0), y(0) {}
    
    Vector2i(int x, int y) 
	: x(x), y(y) {}

    int x;
    int y;
} Vector2i;
    
typedef struct Vector2u {
    Vector2u() : x(0), y(0) {}
    
    Vector2u(uint32_t x, uint32_t y) 
	: x(x), y(y) {}

    uint32_t x;
    uint32_t y;
} Vector2u;
    
inline Vector2u operator+ (const Vector2u& a, const Vector2u& b) {
    return Vector2u {
        a.x + b.x,
        a.y + b.y
    };
}
    
inline Vector2f operator+ (const Vector2f& a, const Vector2f& b) {
    return Vector2f {
        a.x + b.x,
        a.y + b.y
    };
}
    
inline Vector2f operator- (const Vector2f& a, const Vector2f& b) {
    return Vector2f {
        a.x - b.x,
        a.y - b.y
    };
}
    
inline Vector2f operator* (const Vector2f& a, const Vector2f& b) {
    return Vector2f {
        a.x * b.x,
        a.y * b.y
    };
}
    
inline Vector2f operator/ (const Vector2f& a, const Vector2f& b) {
    return Vector2f {
        a.x / b.x,
        a.y / b.y
    };
}

template <typename T>
Vector2f operator *(const T s, Vector2f v) {
    return Vector2f {
        v.x * s,
        v.y * s
    };
}

template <typename T>
Vector2f operator* (Vector2f v, const T s) {
    return s * v;
}
    
template <typename T>
Vector2f operator / (Vector2f v, const T s) {
    return Vector2f {
        v.x / s,
        v.y / s
    };
}
    
inline Vector3f operator+ (const Vector3f& a, const Vector3f& b) {
    return Vector3f {
        a.x + b.x,
        a.y + b.y,
        a.z + b.z
    };
}

inline Vector3f operator - (const Vector3f& a, const Vector3f& b) {
    return Vector3f {
        a.x - b.x,
        a.y - b.y,
        a.z - b.z
    };
}
    
    
inline Vector3f operator* (const Vector3f& a, const Vector3f& b) {
    return Vector3f {
        a.x * b.x,
        a.y * b.y,
        a.z * b.z
    };
}
    
inline Vector3f operator/ (const Vector3f& a, const Vector3f& b) {
    return Vector3f {
        a.x / b.x,
        a.y / b.y,
        a.z / b.z
    };
}

template <typename T>
Vector3f operator *(const T s, Vector3f v) {
    return Vector3f {
        v.x * s,
        v.y * s,
        v.z * s
    };
}

template <typename T>
Vector3f operator* (Vector3f v, const T s) {
    return s * v;
}
    
template <typename T>
Vector3f operator / (Vector3f v, const T s) {
    return Vector3f {
        v.x / s,
        v.y / s,
        v.z / s
    };
}
    
template <typename T>
Vector3f operator +(const T s, Vector3f v) {
    return Vector3f {
        v.x + s,
        v.y + s,
        v.z + s
    };
}

template <typename T>
Vector3f operator+ (Vector3f v, const T s) {
    return s + v;
}
    
template <typename T>
Vector3f operator- (Vector3f v, const T s) {
    return -s + v;
}


// row-major
typedef struct Matrix4x4f {

    float _00, _01, _02, _03;
    float _10, _11, _12, _13;
    float _20, _21, _22, _23;
    float _30, _31, _32, _33;

    Matrix4x4f Transpose() const {
        return Matrix4x4f {
            _00, _10, _20, _30,
            _01, _11, _21, _31,
            _02, _12, _22, _32,
            _03, _13, _23, _33
        };
    }

    static Matrix4x4f Identity() {
        return Matrix4x4f {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
    }

    Vector4f Row(int I) const {
        switch(I) {
        case 0:
            return {_00, _01, _02, _03};
        case 1:
            return {_10, _11, _12, _13};
        case 2:
            return {_20, _21, _22, _23};
        case 3:
            return {_30, _31, _32, _33};
        default:
            break;
        }
        return {0,0,0,0};
    }
    
    Vector4f Col(int I) const {
        switch(I) {
        case 0:
            return {_00, _10, _20, _30};
        case 1:
            return {_01, _11, _21, _31};
        case 2:
            return {_02, _12, _22, _32};
        case 3:
            return {_03, _13, _23, _33};
        default:
            break;
        }
        return {0,0,0,0};
    }

    Matrix4x4f Inverse() {
        const float m[16] = {
            _00, _01, _02, _03,
            _10, _11, _12, _13,
            _20, _21, _22, _23,
            _30, _31, _32, _33,
        };

        float invOut[16] = {};

        gluInvertMatrix(m, invOut);

        Matrix4x4f out {
            invOut[0], invOut[1], invOut[2], invOut[3],
            invOut[4], invOut[5], invOut[6], invOut[7],
            invOut[8], invOut[9], invOut[10], invOut[11],
            invOut[12], invOut[13], invOut[14], invOut[15],
        };

        return out;
    }
private:
    bool gluInvertMatrix(const float m[16], float invOut[16])
    {
        double inv[16], det;
        int i;

        inv[0] = m[5]  * m[10] * m[15] - 
                 m[5]  * m[11] * m[14] - 
                 m[9]  * m[6]  * m[15] + 
                 m[9]  * m[7]  * m[14] +
                 m[13] * m[6]  * m[11] - 
                 m[13] * m[7]  * m[10];

        inv[4] = -m[4]  * m[10] * m[15] + 
                  m[4]  * m[11] * m[14] + 
                  m[8]  * m[6]  * m[15] - 
                  m[8]  * m[7]  * m[14] - 
                  m[12] * m[6]  * m[11] + 
                  m[12] * m[7]  * m[10];

        inv[8] = m[4]  * m[9] * m[15] - 
                 m[4]  * m[11] * m[13] - 
                 m[8]  * m[5] * m[15] + 
                 m[8]  * m[7] * m[13] + 
                 m[12] * m[5] * m[11] - 
                 m[12] * m[7] * m[9];

        inv[12] = -m[4]  * m[9] * m[14] + 
                   m[4]  * m[10] * m[13] +
                   m[8]  * m[5] * m[14] - 
                   m[8]  * m[6] * m[13] - 
                   m[12] * m[5] * m[10] + 
                   m[12] * m[6] * m[9];

        inv[1] = -m[1]  * m[10] * m[15] + 
                  m[1]  * m[11] * m[14] + 
                  m[9]  * m[2] * m[15] - 
                  m[9]  * m[3] * m[14] - 
                  m[13] * m[2] * m[11] + 
                  m[13] * m[3] * m[10];

        inv[5] = m[0]  * m[10] * m[15] - 
                 m[0]  * m[11] * m[14] - 
                 m[8]  * m[2] * m[15] + 
                 m[8]  * m[3] * m[14] + 
                 m[12] * m[2] * m[11] - 
                 m[12] * m[3] * m[10];

        inv[9] = -m[0]  * m[9] * m[15] + 
                  m[0]  * m[11] * m[13] + 
                  m[8]  * m[1] * m[15] - 
                  m[8]  * m[3] * m[13] - 
                  m[12] * m[1] * m[11] + 
                  m[12] * m[3] * m[9];

        inv[13] = m[0]  * m[9] * m[14] - 
                  m[0]  * m[10] * m[13] - 
                  m[8]  * m[1] * m[14] + 
                  m[8]  * m[2] * m[13] + 
                  m[12] * m[1] * m[10] - 
                  m[12] * m[2] * m[9];

        inv[2] = m[1]  * m[6] * m[15] - 
                 m[1]  * m[7] * m[14] - 
                 m[5]  * m[2] * m[15] + 
                 m[5]  * m[3] * m[14] + 
                 m[13] * m[2] * m[7] - 
                 m[13] * m[3] * m[6];

        inv[6] = -m[0]  * m[6] * m[15] + 
                  m[0]  * m[7] * m[14] + 
                  m[4]  * m[2] * m[15] - 
                  m[4]  * m[3] * m[14] - 
                  m[12] * m[2] * m[7] + 
                  m[12] * m[3] * m[6];

        inv[10] = m[0]  * m[5] * m[15] - 
                  m[0]  * m[7] * m[13] - 
                  m[4]  * m[1] * m[15] + 
                  m[4]  * m[3] * m[13] + 
                  m[12] * m[1] * m[7] - 
                  m[12] * m[3] * m[5];

        inv[14] = -m[0]  * m[5] * m[14] + 
                   m[0]  * m[6] * m[13] + 
                   m[4]  * m[1] * m[14] - 
                   m[4]  * m[2] * m[13] - 
                   m[12] * m[1] * m[6] + 
                   m[12] * m[2] * m[5];

        inv[3] = -m[1] * m[6] * m[11] + 
                  m[1] * m[7] * m[10] + 
                  m[5] * m[2] * m[11] - 
                  m[5] * m[3] * m[10] - 
                  m[9] * m[2] * m[7] + 
                  m[9] * m[3] * m[6];

        inv[7] = m[0] * m[6] * m[11] - 
                 m[0] * m[7] * m[10] - 
                 m[4] * m[2] * m[11] + 
                 m[4] * m[3] * m[10] + 
                 m[8] * m[2] * m[7] - 
                 m[8] * m[3] * m[6];

        inv[11] = -m[0] * m[5] * m[11] + 
                   m[0] * m[7] * m[9] + 
                   m[4] * m[1] * m[11] - 
                   m[4] * m[3] * m[9] - 
                   m[8] * m[1] * m[7] + 
                   m[8] * m[3] * m[5];

        inv[15] = m[0] * m[5] * m[10] - 
                  m[0] * m[6] * m[9] - 
                  m[4] * m[1] * m[10] + 
                  m[4] * m[2] * m[9] + 
                  m[8] * m[1] * m[6] - 
                  m[8] * m[2] * m[5];

        det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

        if (det == 0)
            return false;

        det = 1.0 / det;

        for (i = 0; i < 16; i++)
            invOut[i] = inv[i] * det;

        return true;
    }
} Matrix4x4f;

inline Matrix4x4f operator* (const Matrix4x4f& A, const Matrix4x4f& B) {
    const Vector4f A_R0 = A.Row(0);
    const Vector4f A_R1 = A.Row(1);
    const Vector4f A_R2 = A.Row(2);
    const Vector4f A_R3 = A.Row(3);
    
    const Vector4f B_C0 = B.Col(0);
    const Vector4f B_C1 = B.Col(1);
    const Vector4f B_C2 = B.Col(2);
    const Vector4f B_C3 = B.Col(3);
    
    return {
        A_R0.Dot(B_C0), A_R0.Dot(B_C1), A_R0.Dot(B_C2), A_R0.Dot(B_C3), 
        A_R1.Dot(B_C0), A_R1.Dot(B_C1), A_R1.Dot(B_C2), A_R1.Dot(B_C3), 
        A_R2.Dot(B_C0), A_R2.Dot(B_C1), A_R2.Dot(B_C2), A_R2.Dot(B_C3), 
        A_R3.Dot(B_C0), A_R3.Dot(B_C1), A_R3.Dot(B_C2), A_R3.Dot(B_C3), 
    };
}

inline Matrix4x4f TranslationMatrix4x4(Vector3f translation) {
    return Matrix4x4f {
        1, 0, 0, translation.x,
        0, 1, 0, translation.y,
        0, 0, 1, translation.z,
        0, 0, 0, 1,
    };
}
    
inline Matrix4x4f ScaleMatrix4x4(Vector3f scale) {
    return Matrix4x4f {
        scale.x, 0, 0, 0,
        0, scale.y, 0, 0,
        0, 0, scale.z, 0,
        0, 0, 0, 1,
    };
}
    
inline Matrix4x4f RotationMatrix_RH_ZUp_ZAxis(float theta_rad) {
    return Matrix4x4f {
        cos(theta_rad), -sin(theta_rad), 0, 0,
        sin(theta_rad), cos(theta_rad), 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
}
    
inline Matrix4x4f RotationMatrix_RH_ZUp_XAxis(float theta_rad) {
    return Matrix4x4f {
        1, 0, 0, 0,
        0, cos(theta_rad), -sin(theta_rad), 0,
        0, sin(theta_rad), cos(theta_rad), 0,
        0, 0, 0, 1,
    };
}

// z-up, right-handed, frustum is facing the +y axis
inline Matrix4x4f PerspectiveProjectionMatrix4x4_RH_ZUp_ForwardY_HFOV(float aspect_ratio, float horizontal_fov_deg, float near_z, float far_z, float depth0, float depth1) {
    const float horizontal_fov_rad = horizontal_fov_deg * 2 * std::numbers::pi_v<float> / 360.0f; 
    // derived with the goal of having the final z-component in
    // the form of Ay + B, which after perspective divide will become A + B/y
    // and mapping that value to [depth0, depth1]
    // const float A = (far_z * depth1 - near_z * depth0) / (far_z - near_z);
    // const float B = ((depth0 - depth1) * (near_z * far_z)) / (far_z - near_z);
    
    const float A = (near_z * depth0 - far_z * depth1) / (near_z - far_z);
    const float B = ((depth1 - depth0) * (near_z * far_z)) / (near_z - far_z);
    
    return {
        1/std::tan(horizontal_fov_rad/2.f), 0, 0, 0, // x inside frustum (w.r.t. h-fov) is mapped to [-1, 1]
        
        0, 0, aspect_ratio/std::tan(horizontal_fov_rad/2.f), 0, // z inside frustum mapped to [-1,1] s.t. when z==1/AR => z=1 and vice-versa
        // note we map 1/AR to 1 since screen_height = screen_width / AR, and when screen_width == 2 (mapping from -1 to 1)
        // then a height of 2/AR is needed => we want to see all points from -1/AR to 1/AR
        
        0, A, 0, B, // perspective divide will cause y to be mapped to [depth0, depth1]
        
        0, 1, 0, 0 // w becomes y
    };
}
    
inline Matrix4x4f PerspectiveProjectionMatrix4x4_RH_ZUp_ForwardY_Symmetric(float right, float top, float near_z, float far_z, float depth0, float depth1) {
    const float A = (near_z * depth0 - far_z * depth1) / (near_z - far_z);
    const float B = ((depth1 - depth0) * (near_z * far_z)) / (near_z - far_z);
    
    return {
        near_z/right, 0, 0, 0, // x inside frustum (w.r.t. h-fov) is mapped to [-1, 1]
        
        0, 0, near_z/top, 0, // z inside frustum mapped to [-1,1] s.t. when z==1/AR => z=1 and vice-versa
        // note we map 1/AR to 1 since screen_height = screen_width / AR, and when screen_width == 2 (mapping from -1 to 1)
        // then a height of 2/AR is needed => we want to see all points from -1/AR to 1/AR
        
        0, A, 0, B, // perspective divide will cause y to be mapped to [depth0, depth1]
        
        0, 1, 0, 0 // w becomes y
    };
}

inline Matrix4x4f LookAtViewMatrix_RH_ZUp(const Vector3f eye_pos_ws, const Vector3f cam_fwd) {
    const Vector3f absolute_up = {0, 0, 1};
    
    // ensure normalized
    const Vector3f fwd = cam_fwd.Normal();

    // NOTE: as we take fwd X (0,0,1) we have to make sure fwd is never parallel to the
    // up-axis, which would make the cross product zero. i
    // The area of the parallelogram that 2 parallel vectors make is always 0.
    const Vector3f right = fwd.Cross(absolute_up).Normal();
    const Vector3f up = right.Cross(fwd).Normal();

    const Matrix4x4f rotation_matrix = {
        right.x, fwd.x, up.x, 0,
        right.y, fwd.y, up.y, 0,
        right.z, fwd.z, up.z, 0,
        0,0,0,1
    };

    const Matrix4x4f inv_rotation_matrix = rotation_matrix.Transpose();
    
    const Matrix4x4f inv_translation_matrix = {
        1, 0, 0, -1 * eye_pos_ws.x,
        0, 1, 0, -1 * eye_pos_ws.y,
        0, 0, 1, -1 * eye_pos_ws.z,
        0, 0, 0, 1,
    };

    return inv_rotation_matrix * inv_translation_matrix;
}
    
inline Matrix4x4f OrthographicProjectionMatrix4x4_RH() {
    return Matrix4x4f::Identity();
}

typedef struct Matrix3x3f {
    float _00, _01, _02, _03;
    float _10, _11, _12, _13;
    float _20, _21, _22, _23;
} Matrix3x3f;

/***************************************************************************
* These functions were taken from the MiniEngine.
* Source code available here:
* https://github.com/Microsoft/DirectX-Graphics-Samples/blob/master/MiniEngine/Core/Math/Common.h
* Retrieved: January 13, 2016
**************************************************************************/
template <typename T>
inline T AlignUpWithMask(T value, size_t mask)
{
    return (T)(((size_t)value + mask) & ~mask);
}

template <typename T>
inline T AlignDownWithMask(T value, size_t mask)
{
    return (T)((size_t)value & ~mask);
}

template <typename T>
inline T AlignUp(T value, size_t alignment)
{
    return AlignUpWithMask(value, alignment - 1);
}

template <typename T>
inline T AlignDown(T value, size_t alignment)
{
    return AlignDownWithMask(value, alignment - 1);
}

template <typename T>
inline bool IsAligned(T value, size_t alignment)
{
    return 0 == ((size_t)value & (alignment - 1));
}

template <typename T>
inline T DivideByMultiple(T value, size_t alignment)
{
    return (T)((value + alignment - 1) / alignment);
}
/***************************************************************************/

// TODO: templated versions

inline Vector3f Floor(Vector3f v) {
    Vector3f Ret;
    Ret.x = std::floor(v.x);
    Ret.y = std::floor(v.y);
    Ret.z = std::floor(v.z);
    return Ret;
}
    
inline Vector3f Fract(Vector3f v) {
    Vector3f Ret;
    Ret.x = v.x - std::floor(v.x);
    Ret.y = v.y - std::floor(v.y);
    Ret.z = v.z - std::floor(v.z);
    return Ret;
}

// linearly interpolate
inline float Lerp(float val1, float val2, float alpha) {
    return val1 + (val2 - val1) * alpha;
}

inline Vector3f Mod(Vector3f v, float m) {
    return {
        std::fmodf(v.x, m),
        std::fmodf(v.y, m),
        std::fmodf(v.z, m)
    };
}

inline float Halton(int prime, int index) {
    float result = 0;
    float f = 1.;

    while(index > 0) {
        f = f / (float) prime;
        result += f * (float) (index % prime);
        index = index / prime;
    }

    return result;
}

inline Vector2f Halton2D(int prime1, int prime2, int index) {
    return { Halton(prime1, index), Halton(prime2, index) };
}

inline bool IsPointInAxisAlignedRect(Vector2f point, Vector2f rectPos, Vector2f rectSize) {
    
    return (point.x >= rectPos.x && point.x <= rectPos.x + rectSize.x) &&
           (point.y >= rectPos.y && point.y <= rectPos.y + rectSize.y);
}

} // namespace ninmath



