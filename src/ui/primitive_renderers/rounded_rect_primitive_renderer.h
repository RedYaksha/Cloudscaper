#ifndef UI_PRIMITIVE_RENDERERS_ROUNDED_RECT_PRIMITIVE_RENDERER_H_
#define UI_PRIMITIVE_RENDERERS_ROUNDED_RECT_PRIMITIVE_RENDERER_H_

#include <memory>
#include <vector>

#include "ui_primitive_renderer.h"

class Renderer;
class MemoryAllocator;
class PipelineState;

class RoundedRectPrimitiveRenderer : public UIPrimitiveRenderer {
public:
    RoundedRectPrimitiveRenderer(
        std::shared_ptr<Renderer> renderer,
        std::shared_ptr<MemoryAllocator> memAllocator
    );
    
    void Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) override;
    void SetRoundedRects(const std::vector<RoundedRect>& newRects);
    
private:
    std::weak_ptr<PipelineState> pso_;
    std::vector<RoundedRect> data_;
    std::weak_ptr<DynamicVertexBuffer<RoundedRect>> instBuffer_;
};

#endif // UI_PRIMITIVE_RENDERERS_ROUNDED_RECT_PRIMITIVE_RENDERER_H_
