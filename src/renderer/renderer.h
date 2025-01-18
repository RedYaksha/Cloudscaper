#ifndef RENDERER_RENDERER_H_
#define RENDERER_RENDERER_H_

#include <winrt/windows.foundation.h>
#include <vector>
#include "renderer_types.h"
#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>

#include "root_constant_value.h"
#include "shader_types.h"

class Resource;
class PipelineState;
class Shader;
class GraphicsPipelineState;
class Renderer;

struct RendererInitParams {
    HWND hwnd;

    // TODO: configure the renderer
};

class MemoryAllocator;
class DescriptorAllocator;
class ShaderCompiler;
class PipelineAssembler;

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


struct GraphicsPipelineBuilder {
    GraphicsPipelineBuilder& VertexShader(std::string path) { vertexShaderPath_ = path; return *this; }
    GraphicsPipelineBuilder& HullShader(std::string path) { hullShaderPath_ = path; return *this; }
    GraphicsPipelineBuilder& DomainShader(std::string path) { domainShaderPath_ = path; return *this; }
    GraphicsPipelineBuilder& PixelShader(std::string path) { pixelShaderPath_ = path; return *this; }
    GraphicsPipelineBuilder& RenderTarget(std::shared_ptr<Resource> rt) {
        assert(!useDefaultRenderTarget_.has_value() && "useDefaultRenderTarget already set.");
        renderTarget_ = rt;
        return *this;
    }
    GraphicsPipelineBuilder& DepthBuffer(std::shared_ptr<Resource> db) {
        assert(!useDefaultDepthBuffer_.has_value() && "useDefaultDepthBuffer already set.");
        depthBuffer_ = db;
        return *this;
    }
    GraphicsPipelineBuilder& UseDefaultRenderTarget() {
        assert(!renderTarget_ && "renderTarget already set.");
        useDefaultRenderTarget_ = true;
        return *this;
    }
    GraphicsPipelineBuilder& UseDefaultDepthBuffer() {
        assert(!depthBuffer_ && "depthBuffer already set.");
        useDefaultDepthBuffer_ = true;
        return *this;
    }
#define DefineResourceRegisterBindingFunc(type) \
    GraphicsPipelineBuilder& ## type ##(std::weak_ptr<Resource> res, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0) { \
        const ShaderRegister shaderReg(ResourceDescriptorType:: ## type ##, regSpace, regNum); \
        WINRT_ASSERT(!resMap_.contains(shaderReg)); \
        resMap_.insert({shaderReg, ResourceInfo{.res = res, .bindMethod = method}}); \
        return *this; \
    } \

    DefineResourceRegisterBindingFunc(SRV)
    DefineResourceRegisterBindingFunc(CBV)
    DefineResourceRegisterBindingFunc(UAV)
    DefineResourceRegisterBindingFunc(Sampler)
    
#undef DefineResourceRegisterBindingFunc

    template <typename T>
    GraphicsPipelineBuilder& RootConstant(RootConstantValue<T>& val,
                                          uint16_t regNum,
                                          uint16_t regSpace = 0) {
        const ShaderRegister shaderReg(ResourceDescriptorType::Unknown, regSpace, regNum);
        WINRT_ASSERT(!constantMap_.contains(shaderReg));

        const RootConstantInfo decl {
            .data = val.GetData(),
            .num32BitValues = val.GetSizeIn32BitValues()
        };
        constantMap_.insert({shaderReg, decl});
        return *this;
    }

    GraphicsPipelineBuilder& Sampler(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        
        const ShaderRegister shaderReg(ResourceDescriptorType::Sampler, regSpace, regNum);
        WINRT_ASSERT(!samplerMap_.contains(shaderReg));

        samplerMap_.insert({shaderReg, desc});
        return *this;
    }
    
    GraphicsPipelineBuilder& StaticSampler(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        const ShaderRegister shaderReg(ResourceDescriptorType::Sampler, regSpace, regNum);
        WINRT_ASSERT(!staticSamplerMap_.contains(shaderReg));
        
        staticSamplerMap_.insert({shaderReg, desc});
        return *this;
    }
    
    std::weak_ptr<PipelineState> Build() {
        return buildFunc(*this);
    }

private:
    typedef std::function<std::weak_ptr<PipelineState>(const GraphicsPipelineBuilder&)> BuildFunction;
    friend Renderer;
    GraphicsPipelineBuilder(std::string id, BuildFunction buildFunc)
        : id(id), buildFunc(buildFunc) {}
        
    BuildFunction buildFunc;
    std::string id;
    
    std::shared_ptr<Resource> renderTarget_;
    std::shared_ptr<Resource> depthBuffer_;
    
    std::optional<bool> useDefaultRenderTarget_;
    std::optional<bool> useDefaultDepthBuffer_;
    
    std::optional<std::string> vertexShaderPath_;
    std::optional<std::string> hullShaderPath_;
    std::optional<std::string> domainShaderPath_;
    std::optional<std::string> pixelShaderPath_;

    std::map<ShaderRegister, ResourceInfo> resMap_;
    std::map<ShaderRegister, RootConstantInfo> constantMap_;
    std::map<ShaderRegister, D3D12_SAMPLER_DESC> samplerMap_;
    std::map<ShaderRegister, D3D12_SAMPLER_DESC> staticSamplerMap_;
};

//
// Simple, single-threaded renderer
//
class Renderer {
public:
    static std::shared_ptr<Renderer> CreateRenderer(RendererInitParams params, HRESULT& hr);

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

    struct ComputePipelineParams {
        std::string computeShaderPath;
    };
    void RegisterComputePipeline(std::string id, const ComputePipelineParams& params);
    
    winrt::com_ptr<ID3D12Device> GetDevice() const { return device_; }
    
private:
    Renderer(RendererInitParams params, HRESULT& hr);
    ~Renderer();

    std::weak_ptr<PipelineState> FinalizeGraphicsPipelineBuild(const GraphicsPipelineBuilder& builder);

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
    std::shared_ptr<DescriptorAllocator> resourceDescriptorAllocator_;
    std::shared_ptr<DescriptorAllocator> samplerDescriptorAllocator_;
    std::shared_ptr<ShaderCompiler> shaderCompiler_;
    std::shared_ptr<PipelineAssembler> pipelineAssembler_;
    
    std::map<std::string, std::shared_ptr<PipelineState>> psoMap_;
    std::map<std::string, std::shared_ptr<Shader>> shaderMap_;
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
