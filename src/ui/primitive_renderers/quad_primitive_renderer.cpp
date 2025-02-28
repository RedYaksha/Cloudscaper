#include "quad_primitive_renderer.h"

#include "pipeline_builder.h"
#include "renderer.h"
#include "memory/memory_allocator.h"
#include "pipeline_state.h"

QuadPrimitiveRenderer::QuadPrimitiveRenderer(std::shared_ptr<Renderer> renderer,
                                             std::shared_ptr<MemoryAllocator> memAllocator)
        : UIPrimitiveRenderer(renderer, memAllocator) {

    quadData_.resize(100);
    
    VertexBufferLayout quadInstLayout({
        {"COLOR", 1, ShaderDataType::Float4},
        {"TRANSFORM", 1, ShaderDataType::Float4},
    });
                                                                                
    quadInstBuffer_ = memAllocator_->CreateResource<DynamicVertexBuffer<Quad>>("UIFramework_Quad_Instance_Buffer",
                                                                                   quadData_,
                                                                                   quadInstLayout,
                                                                                   VertexBufferUsage::PerInstance,
                                                                                  D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    WINRT_ASSERT(quadInstBuffer_.lock());
    WINRT_ASSERT(rectVertexBuffer.lock());
    WINRT_ASSERT(rectIndexBuffer.lock());

    quadPso_ = renderer_->BuildGraphicsPipeline("UIFramework_Quad")
    .VertexShader("shaders/ui/quad_vs.hlsl")
    .PixelShader("shaders/ui/quad_ps.hlsl")
    .VertexBuffer(rectVertexBuffer, 0)
    .VertexBuffer(quadInstBuffer_, 1)
    .IndexBuffer(rectIndexBuffer)
    .RootConstant(renderer_->GetScreenSizeRootConstantValue(), 0)
    //.UseDefaultDepthBuffer()
    .UseDefaultRenderTarget()
    .Build();
}

void QuadPrimitiveRenderer::Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    if(quadData_.size() <= 0) {
        return;
    }
    
    renderer_->ExecuteGraphicsPipeline(cmdList, quadPso_.lock(), quadData_.size());
}

void QuadPrimitiveRenderer::SetQuads(const std::vector<Quad>& newQuads) {
    quadData_ = newQuads;
    quadInstBuffer_.lock()->UpdateGPUData();
    std::static_pointer_cast<GraphicsPipelineState>(quadPso_.lock())->InitializeVertexAndIndexBufferDescriptors();
}
