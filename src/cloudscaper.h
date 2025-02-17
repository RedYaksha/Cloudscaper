#ifndef CLOUDSCAPER_CLOUDSCAPER_H_
#define CLOUDSCAPER_CLOUDSCAPER_H_

#include "application.h"
#include "root_constant_value.h"
#include "ninmath/ninmath.h"

class Resource;
class MemoryAllocator;
class Renderer;
class UIFramework;
class PipelineState;
class GraphicsPipelineState;
class DepthBuffer;
class RenderTarget;

class VertexBufferBase;
class IndexBufferBase;

struct BasicVertexData {
    ninmath::Vector4f pos;
    ninmath::Vector2f uv;
};

class Cloudscaper : public Application {
public:
    Cloudscaper(HINSTANCE hinst);
    ~Cloudscaper();
    virtual void Tick(double deltaTime) override;

private:
    std::weak_ptr<Resource> imageTex_;
    std::weak_ptr<Resource> computeTex_;
    std::weak_ptr<VertexBufferBase> vertexBuffer_;
    std::weak_ptr<IndexBufferBase> indexBuffer_;
    // std::weak_ptr<DepthBuffer> renderTarget_;

    std::vector<BasicVertexData> vertices_;
    std::vector<uint32_t> indices_;

    std::shared_ptr<Window> mainWindow_;
    std::shared_ptr<Renderer> renderer_;
    std::shared_ptr<UIFramework> uiFramework_;
    std::shared_ptr<MemoryAllocator> memAllocator_;
    std::weak_ptr<PipelineState> testPso_;
    std::weak_ptr<PipelineState> testComputePso_;
    RootConstantValue<float> testConstVal_;
    
    RootConstantValue<ninmath::Vector2f> screenSizeRCV_;

    std::weak_ptr<Resource> transmittanceLUT_;
    std::weak_ptr<Resource> multiScatteringLUT_;
    std::weak_ptr<Resource> skyViewLUT_;

    std::weak_ptr<PipelineState> transmittanceCPSO_;
    
};


#endif // CLOUDSCAPER_CLOUDSCAPER_H_
