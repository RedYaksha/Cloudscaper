#include "quad_primitive_renderer.h"

#include "pipeline_builder.h"
#include "renderer.h"
#include "memory/memory_allocator.h"

std::vector<BasicVertex> QuadPrimitiveRenderer::quadVertices = {
    {.pos=ninmath::Vector4f{0.0f, 0.0f, 0.f, 1.f}, .uv= ninmath::Vector2f{0.f, 1.f}},
    {.pos=ninmath::Vector4f{0.0f, 1.0f, 0.f, 1.f}, .uv=ninmath::Vector2f{0.f, 0.f}},
    {.pos=ninmath::Vector4f{1.0f, 0.0f, 0.f, 1.f}, .uv=ninmath::Vector2f{1.f, 0.f}},
    {.pos=ninmath::Vector4f{1.0f, 1.0f, 0.f, 1.f}, .uv=ninmath::Vector2f{1.f, 1.f}},
};

// CCW winding order
std::vector<uint16_t> QuadPrimitiveRenderer::quadIndices = {
    0, 1, 2,
    1, 3, 2
};

QuadPrimitiveRenderer::QuadPrimitiveRenderer(std::shared_ptr<Renderer> renderer,
                                             std::shared_ptr<MemoryAllocator> memAllocator)
        : renderer_(renderer), memAllocator_(memAllocator) {

    quadData_.resize(100);
    quadData_[0] = Quad {.color = {1,0,1,1}, .transform = {0, 20, 400, 20}, };
    quadData_[1] = Quad {.color = {1,0,1,1}, .transform = {0, 0, 50, 50}, };
    quadData_[2] = Quad {.color = {1,0,1,1}, .transform = {0, 0, 50, 50}, };
    
    VertexBufferLayout quadVertexLayout({
        {"POSITION", 0, ShaderDataType::Float4},
        {"UV", 0, ShaderDataType::Float2},
    });
    
    VertexBufferLayout quadInstLayout({
        {"COLOR", 1, ShaderDataType::Float4},
        {"TRANSFORM", 1, ShaderDataType::Float4},
    });
    
    quadVertexBuffer_ = memAllocator_->CreateResource<StaticVertexBuffer<BasicVertex>>("UIFramework_Quad_Vertex_Buffer",
                                                                                QuadPrimitiveRenderer::quadVertices,
                                                                                quadVertexLayout,
                                                                                VertexBufferUsage::PerVertex,
                                                                                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    WINRT_ASSERT(quadVertexBuffer_.lock());
                                                                                
    quadInstBuffer_ = memAllocator_->CreateResource<DynamicVertexBuffer<Quad>>("UIFramework_Quad_Instance_Buffer",
                                                                                   quadData_,
                                                                                   quadInstLayout,
                                                                                   VertexBufferUsage::PerInstance,
                                                                                  D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    WINRT_ASSERT(quadInstBuffer_.lock());
    
    quadIndexBuffer_ = memAllocator_->CreateResource<IndexBuffer<uint16_t>>("IB", QuadPrimitiveRenderer::quadIndices);

    quadPso_ = renderer_->BuildGraphicsPipeline("UIFramework_Quad")
    .VertexShader("shaders/ui/quad_vs.hlsl")
    .PixelShader("shaders/ui/quad_ps.hlsl")
    .VertexBuffer(quadVertexBuffer_, 0)
    .VertexBuffer(quadInstBuffer_, 1)
    .IndexBuffer(quadIndexBuffer_)
    .RootConstant(renderer_->GetScreenSizeRootConstantValue(), 0)
    .UseDefaultDepthBuffer()
    .UseDefaultRenderTarget()
    .Build();
}

void QuadPrimitiveRenderer::Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    renderer_->ExecuteGraphicsPipeline(cmdList, quadPso_.lock(), quadData_.size());
}

void QuadPrimitiveRenderer::SetQuads(const std::vector<Quad>& newQuads) {
    quadData_ = newQuads;
    quadInstBuffer_.lock()->UpdateGPUData();
}
