#ifndef UI_UI_PRIMITIVE_RENDERER_H_
#define UI_UI_PRIMITIVE_RENDERER_H_

#include <memory>
#include <vector>

#include "resources.h"
#include "ui_primitives.h"

class Renderer;
class MemoryAllocator;
class PipelineState;

class UIPrimitiveRenderer {
public:
    virtual ~UIPrimitiveRenderer() = default;

    UIPrimitiveRenderer(std::shared_ptr<Renderer> renderer, std::shared_ptr<MemoryAllocator> memAllocator);

    virtual void Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) = 0;
    
protected:
    static std::vector<BasicVertex> rectVertices;
    static std::vector<uint16_t> rectIndices;
    
    static std::weak_ptr<StaticVertexBuffer<BasicVertex>> rectVertexBuffer;
    static std::weak_ptr<IndexBuffer<uint16_t>> rectIndexBuffer;
    static VertexBufferLayout rectVertexBufferLayout;
    
    std::shared_ptr<Renderer> renderer_;
    std::shared_ptr<MemoryAllocator> memAllocator_;
};

#endif UI_UI_PRIMITIVE_RENDERER_H_
