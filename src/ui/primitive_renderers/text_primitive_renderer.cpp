#include "text_primitive_renderer.h"

#include "pipeline_builder.h"
#include "renderer.h"
#include "renderer_common.h"
#include "memory/memory_allocator.h"
#include "pipeline_state.h"

TextRectPrimitiveRenderer::TextRectPrimitiveRenderer(
    std::shared_ptr<Renderer> renderer,
    std::shared_ptr<MemoryAllocator> memAllocator,
    std::weak_ptr<Resource> fontRes)
    : UIPrimitiveRenderer(renderer, memAllocator) {
    
    VertexBufferLayout textInstLayout({
        {"COLOR", 0, ShaderDataType::Float4},
        {"TRANSFORM", 0, ShaderDataType::Float4},
        {"CLIP_TRANSFORM", 0, ShaderDataType::Float4},
        {"UV_START", 0, ShaderDataType::Float2},
        {"UV_END", 0, ShaderDataType::Float2},
    });

    textData_.resize(100);
    textInstBuffer_ = memAllocator_->CreateResource<DynamicVertexBuffer<TextRect>>("UIFramework_Text_Rect_Instance_Buffer",
                                                                                   textData_,
                                                                                   textInstLayout,
                                                                                   VertexBufferUsage::PerInstance,
                                                                                  D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    WINRT_ASSERT(textInstBuffer_.lock());
    WINRT_ASSERT(rectVertexBuffer.lock());
    WINRT_ASSERT(rectIndexBuffer.lock());

    D3D12_BLEND_DESC blendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    blendState.RenderTarget[0].BlendEnable = TRUE;
    blendState.RenderTarget[0].LogicOpEnable = FALSE;
    blendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;

    textRectPso_ = renderer_->BuildGraphicsPipeline("UIFramework_Text")
    .VertexShader("shaders/ui/text_rect_vs.hlsl")
    .PixelShader("shaders/ui/text_rect_ps.hlsl")
    .VertexBuffer(rectVertexBuffer, 0)
    .VertexBuffer(textInstBuffer_, 1)
    .IndexBuffer(rectIndexBuffer)
    .RootConstant(renderer_->GetScreenSizeRootConstantValue(), 0)
    .SRV(fontRes, 0)
    .StaticSampler(renderer_common::samplerLinearClamp, 0)
    .BlendState(blendState)
    .UseDefaultRenderTarget()
    .Build();
}

void TextRectPrimitiveRenderer::Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    if(textData_.size() <= 0) {
        return;
    }
    
    renderer_->ExecuteGraphicsPipeline(cmdList, textRectPso_.lock(), textData_.size());
}

void TextRectPrimitiveRenderer::SetTextRects(const std::vector<TextRect>& newTextRects) {
    textData_ = newTextRects;
    
    textInstBuffer_.lock()->UpdateGPUData();
    std::static_pointer_cast<GraphicsPipelineState>(textRectPso_.lock())->InitializeVertexAndIndexBufferDescriptors();
}
