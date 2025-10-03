#ifndef RENDERER_RENDERER_H_
#define RENDERER_RENDERER_H_

#include <winrt/windows.foundation.h>
#include <vector>
#include "renderer_types.h"
#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <string>

#include "root_constant_value.h"
#include "shader_types.h"
#include "ninmath/ninmath.h"

class Resource;
class PipelineState;
class GraphicsPipelineState;
class Renderer;
class Shader;
class RenderTarget;
class VertexBufferBase;
class IndexBufferBase;
class DepthBuffer;


struct RendererConfig {
    DXGI_FORMAT swapChainFormat;
    uint8_t numBuffers;

    RendererConfig()
        :
    swapChainFormat(DXGI_FORMAT_R8G8B8A8_UNORM),
    numBuffers(2)
    {}
};


class MemoryAllocator;
class DescriptorAllocator;
class ShaderCompiler;
class PipelineAssembler;

class GraphicsPipelineBuilder;
class ComputePipelineBuilder;

template<typename T>
concept IsMemoryAllocatorImpl = requires
{
    std::derived_from<T, MemoryAllocator>;
    !std::is_abstract_v<T>;
};

template<typename T>
concept IsDescriptorAllocatorImpl = requires
{
    std::derived_from<T, DescriptorAllocator>;
    !std::is_abstract_v<T>;
};

//
// Simple, single-threaded renderer
//
class Renderer {
public:
    static const ResourceID SwapChainRenderTargetID;
    static const ResourceID DefaultDepthStencilTargetID;
    
    static std::shared_ptr<Renderer> CreateRenderer(HWND hwnd, RendererConfig params, HRESULT& hr);

    template<IsMemoryAllocatorImpl T, class... _Types>
    requires std::is_constructible_v<T, _Types...>
    std::shared_ptr<T> InitializeMemoryAllocator(_Types&&... args);
    
    template<IsDescriptorAllocatorImpl T, class... _Types>
    requires std::is_constructible_v<T, _Types...>
    std::shared_ptr<T> InitializeResourceDescriptorAllocator(_Types&&... args);
    
    template<IsDescriptorAllocatorImpl T, class... _Types>
    requires std::is_constructible_v<T, _Types...>
    std::shared_ptr<T> InitializeSamplerDescriptorAllocator(_Types&&... args);

    winrt::com_ptr<ID3D12GraphicsCommandList> StartCommandList(HRESULT& hr);
    void FinishCommandList(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, HRESULT& hr);

    // was this renderer able to instantiate all needed variables?
    // (able to find a valid adapter, create device, etc.)

    // Checks registered shaders for file changes,
    //
    void Tick(double deltaTime);

    GraphicsPipelineBuilder BuildGraphicsPipeline(std::string id);
    ComputePipelineBuilder BuildComputePipeline(std::string id);

    winrt::com_ptr<ID3D12Device> GetDevice() const { return device_; }

    void ExecutePipeline(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, std::shared_ptr<PipelineState> pso);
    void ExecuteGraphicsPipeline(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, std::shared_ptr<PipelineState> pso, uint32_t numInstances);


    const RootConstantValue<ninmath::Vector2f>& GetScreenSizeRootConstantValue() const { return screenSizeRCV_; }
    
    const ninmath::Vector2f GetScreenSize() const {
        ninmath::Vector2f v = screenSizeRCV_.GetValue();
        return { v.x, v.y };
    }

    std::shared_ptr<RenderTarget> CreateRenderTarget(ResourceID id, DXGI_FORMAT format, bool useAsUAV, D3D12_RESOURCE_STATES state);

    std::shared_ptr<RenderTarget> GetCurrentSwapChainBufferResource() const;
    
private:
    Renderer(HWND hwnd, RendererConfig config, HRESULT& hr);
    ~Renderer();

    std::weak_ptr<PipelineState> FinalizeGraphicsPipelineBuild(const GraphicsPipelineBuilder& builder);
    std::weak_ptr<PipelineState> FinalizeComputePipelineBuild(const ComputePipelineBuilder& builder);
    bool CreateRenderTargetDescriptorAllocation(const RenderTargetGroupID& id, bool isSwapChain);
    
    // called in InitializeMemoryAllocator<T>
    void OnMemoryAllocatorSet();

    void InitializeDefaultDepthBuffers();

    void PrepareGraphicsPipelineRenderTargets(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, std::shared_ptr<GraphicsPipelineState> pso);


    uint32_t clientWidth_;
    uint32_t clientHeight_;
    winrt::com_ptr<ID3D12Device2> device_;
    winrt::com_ptr<IDXGISwapChain4> swapChain_;
    
    winrt::com_ptr<ID3D12CommandQueue> cmdQueue_;
    winrt::com_ptr<ID3D12GraphicsCommandList> cmdList_;
    std::vector<winrt::com_ptr<ID3D12CommandAllocator>> cmdAllocators_;
    bool cmdListActive_;
    
    winrt::com_ptr<ID3D12CommandQueue> cmdCopyQueue_;
    winrt::com_ptr<ID3D12GraphicsCommandList> cmdCopyList_;
    std::vector<winrt::com_ptr<ID3D12CommandAllocator>> cmdCopyAllocators_;

    std::vector<uint64_t> mainFenceValues_;
    winrt::com_ptr<ID3D12Fence> mainFence_;
    uint64_t fenceValue_;
    HANDLE fenceEvent_;
    
    uint32_t curBackBufferIndex_;
    uint32_t numBuffers_;
    
    std::shared_ptr<MemoryAllocator> memoryAllocator_;
    std::shared_ptr<MemoryAllocator> depthBufferMemoryAllocator_;
    std::shared_ptr<MemoryAllocator> renderTargetMemoryAllocator_;
    
    std::shared_ptr<DescriptorAllocator> resourceDescriptorAllocator_;
    std::shared_ptr<DescriptorAllocator> samplerDescriptorAllocator_;
    std::shared_ptr<DescriptorAllocator> renderTargetDescriptorAllocator_;
    std::shared_ptr<DescriptorAllocator> depthStencilDescriptorAllocator_;
    
    std::shared_ptr<ShaderCompiler> shaderCompiler_;
    std::shared_ptr<PipelineAssembler> pipelineAssembler_;
    
    std::map<std::string, std::shared_ptr<PipelineState>> psoMap_;
    std::map<std::string, std::shared_ptr<Shader>> shaderMap_;
    std::unordered_map<ResourceID, std::shared_ptr<RenderTargetHandle>> renderTargetMap_;
    std::unordered_map<ResourceID, std::shared_ptr<DepthStencilTargetHandle>> depthStencilTargetMap_;

    // render target group -> array of descriptor allocations (1 per swap chain buffer)
    std::unordered_map<RenderTargetGroupID,
                       std::vector<std::weak_ptr<class DescriptorHeapAllocation>>, RenderTargetGroupIDHasher> renderTargetAllocMap_;
    
    // depth buffer -> array of descriptor allocations (1 per swap chain buffer)
    std::unordered_map<ResourceID,
                       std::vector<std::weak_ptr<class DescriptorHeapAllocation>>> depthBufferAllocMap_;

    std::set<RenderTargetGroupID> curFrameRenderTargetsReset_;
    
    // TODO: custom non-render target resource (e.g. UAV) where there's 1 allocation per swap chain buffer

    RendererConfig config_;
    D3D12_RECT scissorRect_;
    D3D12_VIEWPORT viewport_;

    RootConstantValue<ninmath::Vector2f> screenSizeRCV_;
};

template<IsMemoryAllocatorImpl T, class... _Types>
requires std::is_constructible_v<T, _Types...>
std::shared_ptr<T> Renderer::InitializeMemoryAllocator(_Types&&... args) {
    if(memoryAllocator_) {
        return nullptr;
    }

    // template is basically the definition of std::make_shared
    std::shared_ptr<T> newAllocator = std::make_shared<T>(std::forward<_Types>(args)...);
    memoryAllocator_ = newAllocator;
    
    OnMemoryAllocatorSet();

    return newAllocator;
}

template <IsDescriptorAllocatorImpl T, class ... _Types>
requires std::is_constructible_v<T, _Types...>
std::shared_ptr<T> Renderer::InitializeResourceDescriptorAllocator(_Types&&... args) {
    if(resourceDescriptorAllocator_) {
        return nullptr;
    }

    // template is basically the definition of std::make_shared
    std::shared_ptr<T> newAllocator = std::make_shared<T>(std::forward<_Types>(args)...);
    resourceDescriptorAllocator_ = newAllocator;

    return newAllocator;
}

template <IsDescriptorAllocatorImpl T, class ... _Types>
requires std::is_constructible_v<T, _Types...>
std::shared_ptr<T> Renderer::InitializeSamplerDescriptorAllocator(_Types&&... args) {
    if(samplerDescriptorAllocator_) {
        return nullptr;
    }

    // template is basically the definition of std::make_shared
    std::shared_ptr<T> newAllocator = std::make_shared<T>(std::forward<_Types>(args)...);
    samplerDescriptorAllocator_ = newAllocator;

    return newAllocator;
}


#endif // RENDERER_RENDERER_H_
