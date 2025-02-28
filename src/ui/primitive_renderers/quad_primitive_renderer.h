#ifndef RENDERER_UI_QUAD_PRIMITIVE_RENDERER_H_
#define RENDERER_UI_QUAD_PRIMITIVE_RENDERER_H_

#include <memory>
#include <vector>

#include "resources.h"
#include "ui_primitives.h"
#include "ui_primitive_renderer.h"


class Renderer;
class MemoryAllocator;
class PipelineState;

class QuadPrimitiveRenderer : public UIPrimitiveRenderer {
public:
    QuadPrimitiveRenderer(std::shared_ptr<Renderer> renderer, std::shared_ptr<MemoryAllocator> memAllocator);
    void Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) override;
    void SetQuads(const std::vector<Quad>& newQuads);

private:
    std::weak_ptr<PipelineState> quadPso_;
    std::vector<Quad> quadData_;
    std::weak_ptr<DynamicVertexBuffer<Quad>> quadInstBuffer_;
};

#endif // RENDERER_UI_QUAD_PRIMITIVE_RENDERER_H_
