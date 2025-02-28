#ifndef RENDERER_UI_UI_PRIMITIVES_H_
#define RENDERER_UI_UI_PRIMITIVES_H_

#include "ninmath/ninmath.h"
#include <vector>

struct BasicVertex {
    ninmath::Vector4f pos;
    ninmath::Vector2f uv;
};

inline static std::vector<BasicVertex> quadVertices = {
    {.pos=ninmath::Vector4f{-0.5f, -0.5f, 0.f, 1.f}, .uv= ninmath::Vector2f{0.f, 1.f}},
    {.pos=ninmath::Vector4f{-0.5f, 0.5f, 0.f, 1.f}, .uv=ninmath::Vector2f{0.f, 0.f}},
    {.pos=ninmath::Vector4f{0.5f, 0.5f, 0.f, 1.f}, .uv=ninmath::Vector2f{1.f, 0.f}},
    {.pos=ninmath::Vector4f{0.5f, -0.5f, 0.f, 1.f}, .uv=ninmath::Vector2f{1.f, 1.f}},
};

// CCW winding order
inline static std::vector<uint16_t> quadIndices = {
    0, 2, 1,
    0, 3, 2
};

struct Quad {
    ninmath::Vector4f color;
    ninmath::Vector4f transform;
};

struct RoundedRect {
    ninmath::Vector4f color;
    ninmath::Vector4f transform;
    ninmath::Vector4f radii; // tl, tr, bl, br
};

struct TextRect {
    ninmath::Vector4f color;
    ninmath::Vector4f transform;
    ninmath::Vector4f clipTransform;
    ninmath::Vector2f uvStart;
    ninmath::Vector2f uvEnd;
};

#endif // RENDERER_UI_UI_PRIMITIVES_H_
