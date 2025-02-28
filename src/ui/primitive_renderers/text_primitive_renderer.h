#ifndef UI_TEXT_PRIMITIVE_RENDERER_H_
#define UI_TEXT_PRIMITIVE_RENDERER_H_

#include <memory>

#include "ui_primitive_renderer.h"

class Renderer;
class MemoryAllocator;
class PipelineState;

class TextRectPrimitiveRenderer : public UIPrimitiveRenderer {
public:
    TextRectPrimitiveRenderer(
        std::shared_ptr<Renderer> renderer,
        std::shared_ptr<MemoryAllocator> memAllocator,
        std::weak_ptr<Resource> fontRes // TODO
    );

    void Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) override;
    void SetTextRects(const std::vector<TextRect>& newTextRects);
    
private:
    std::weak_ptr<PipelineState> textRectPso_;
    std::vector<TextRect> textData_;
    std::weak_ptr<DynamicVertexBuffer<TextRect>> textInstBuffer_;
};

#endif // UI_TEXT_PRIMITIVE_RENDERER_H_
