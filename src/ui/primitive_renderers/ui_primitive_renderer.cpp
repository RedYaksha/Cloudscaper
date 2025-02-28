#include "ui_primitive_renderer.h"

#include "memory/memory_allocator.h"

std::vector<BasicVertex> UIPrimitiveRenderer::rectVertices = {
    {.pos=ninmath::Vector4f{0.0f, 0.0f, 0.f, 1.f}, .uv= ninmath::Vector2f{0.f, 0.f}},
    {.pos=ninmath::Vector4f{0.0f, 1.0f, 0.f, 1.f}, .uv=ninmath::Vector2f{0.f, 1.f}},
    {.pos=ninmath::Vector4f{1.0f, 0.0f, 0.f, 1.f}, .uv=ninmath::Vector2f{1.f, 0.f}},
    {.pos=ninmath::Vector4f{1.0f, 1.0f, 0.f, 1.f}, .uv=ninmath::Vector2f{1.f, 1.f}},
};

// CCW winding order
std::vector<uint16_t> UIPrimitiveRenderer::rectIndices = {
    0, 1, 2,
    1, 3, 2
};

VertexBufferLayout UIPrimitiveRenderer::rectVertexBufferLayout = VertexBufferLayout({
    {"POSITION", 0, ShaderDataType::Float4},
    {"UV", 0, ShaderDataType::Float2},
});

std::weak_ptr<StaticVertexBuffer<BasicVertex>> UIPrimitiveRenderer::rectVertexBuffer = std::weak_ptr<StaticVertexBuffer<BasicVertex>>();
std::weak_ptr<IndexBuffer<uint16_t>> UIPrimitiveRenderer::rectIndexBuffer = std::weak_ptr<IndexBuffer<uint16_t>>();

UIPrimitiveRenderer::UIPrimitiveRenderer(std::shared_ptr<Renderer> renderer,
    std::shared_ptr<MemoryAllocator> memAllocator) : renderer_(renderer), memAllocator_(memAllocator) {

    // a rect vertex/index buffer, to be shared amongst the primitives
    if(rectVertexBuffer.expired()) {
        rectVertexBuffer = memAllocator_->CreateResource<StaticVertexBuffer<BasicVertex>>("UIPrimitive_Rect_Vertex_Buffer",
                                                                                UIPrimitiveRenderer::rectVertices,
                                                                                rectVertexBufferLayout,
                                                                                VertexBufferUsage::PerVertex,
                                                                                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    
    if(rectIndexBuffer.expired()) {
        rectIndexBuffer = memAllocator_->CreateResource<IndexBuffer<uint16_t>>("UIPrimitive_Rect_Index_Buffer", UIPrimitiveRenderer::rectIndices);
    }
}
