#include "rounded_rect_primitive_renderer.h"

#include "pipeline_builder.h"
#include "renderer.h"
#include "memory/memory_allocator.h"

RoundedRectPrimitiveRenderer::RoundedRectPrimitiveRenderer(std::shared_ptr<Renderer> renderer,
                                                           std::shared_ptr<MemoryAllocator> memAllocator)
   : UIPrimitiveRenderer(renderer, memAllocator) {

    data_.resize(100);

    VertexBufferLayout instLayout({
        {"COLOR", 1, ShaderDataType::Float4},
        {"TRANSFORM", 1, ShaderDataType::Float4},
        {"RADII", 1, ShaderDataType::Float4},
    });
                                                                                
    instBuffer_ = memAllocator_->CreateResource<DynamicVertexBuffer<RoundedRect>>("UIFramework_RoundedRect_Instance_Buffer",
                                                                                   data_,
                                                                                   instLayout,
                                                                                   VertexBufferUsage::PerInstance,
                                                                                  D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    WINRT_ASSERT(instBuffer_.lock());
    WINRT_ASSERT(rectVertexBuffer.lock());
    WINRT_ASSERT(rectIndexBuffer.lock());

    pso_ = renderer_->BuildGraphicsPipeline("UIFramework_RoundedRect")
    .VertexShader("shaders/ui/rounded_rect_vs.hlsl")
    .PixelShader("shaders/ui/rounded_rect_ps.hlsl")
    .VertexBuffer(rectVertexBuffer, 0)
    .VertexBuffer(instBuffer_, 1)
    .IndexBuffer(rectIndexBuffer)
    .RootConstant(renderer_->GetScreenSizeRootConstantValue(), 0)
    .UseDefaultRenderTarget()
    .Build();
}

void RoundedRectPrimitiveRenderer::Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    if(data_.size() > 0) {
        renderer_->ExecuteGraphicsPipeline(cmdList, pso_.lock(), data_.size());
    }
}

void RoundedRectPrimitiveRenderer::SetRoundedRects(const std::vector<RoundedRect>& newRects) {
    data_ = newRects;
    instBuffer_.lock()->UpdateGPUData();
}
