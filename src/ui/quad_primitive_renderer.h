#ifndef RENDERER_UI_QUAD_PRIMITIVE_RENDERER_H_
#define RENDERER_UI_QUAD_PRIMITIVE_RENDERER_H_

#include <memory>
#include <vector>

#include "resources.h"
#include "ui_primitives.h"


class Renderer;
class MemoryAllocator;
class PipelineState;

class QuadPrimitiveRenderer {
public:
    QuadPrimitiveRenderer(std::shared_ptr<Renderer> renderer, std::shared_ptr<MemoryAllocator> memAllocator);
    void Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList);
    void SetQuads(const std::vector<Quad>& newQuads);

private:
    static std::vector<BasicVertex> quadVertices;
    static std::vector<uint16_t> quadIndices;
    

    
    std::weak_ptr<PipelineState> quadPso_;
    std::vector<Quad> quadData_;
    std::weak_ptr<DynamicVertexBuffer<Quad>> quadInstBuffer_;
    std::weak_ptr<StaticVertexBuffer<BasicVertex>> quadVertexBuffer_;
    std::weak_ptr<IndexBuffer<uint16_t>> quadIndexBuffer_;

    std::shared_ptr<Renderer> renderer_;
    std::shared_ptr<MemoryAllocator> memAllocator_;
};

#endif // RENDERER_UI_QUAD_PRIMITIVE_RENDERER_H_
