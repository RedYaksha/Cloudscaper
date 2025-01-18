#ifndef CLOUDSCAPER_CLOUDSCAPER_H_
#define CLOUDSCAPER_CLOUDSCAPER_H_

#include "application.h"
#include "root_constant_value.h"

class Resource;
class MemoryAllocator;
class Renderer;
class PipelineState;
class GraphicsPipelineState;

class Cloudscaper : public Application {
public:
    Cloudscaper(HINSTANCE hinst);
    virtual void Tick(double deltaTime) override;

private:
    std::shared_ptr<Resource> imageTex_;

    
    std::shared_ptr<Window> mainWindow_;
    std::shared_ptr<Renderer> renderer_;
    std::shared_ptr<MemoryAllocator> memAllocator_;
    std::weak_ptr<PipelineState> testPso_;
    RootConstantValue<int> testConstVal_;
};


#endif // CLOUDSCAPER_CLOUDSCAPER_H_
