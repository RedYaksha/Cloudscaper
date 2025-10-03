#ifndef RENDERER_PIPELINE_BUILDER_H_
#define RENDERER_PIPELINE_BUILDER_H_

#include <string>
#include <unordered_map>
#include "root_constant_value.h"
#include "renderer_types.h"
#include "shader_types.h"
#include "resources.h"

class PipelineState;
class IndexBufferBase;
class VertexBufferBase;
class Renderer;

class ResourceConfiguration {
public:
#define DefineResourceRegisterBindingFunc(type) \
    ResourceConfiguration& type ## (std::weak_ptr<Resource> res, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0) { \
        const ShaderRegister shaderReg(ResourceDescriptorType:: ## type ##, regSpace, regNum); \
        WINRT_ASSERT(!resMap_.contains(shaderReg)); \
        resMap_.insert({shaderReg, ResourceInfo{.res = res, .bindMethod = method, .descriptorConfig = nullptr }}); \
        return *this; \
    } \
    
#define DefineResourceRegisterBindingFuncWithConfig(type) \
    template <IsDescriptorConfiguration T> \
    ResourceConfiguration& type ## (std::weak_ptr<Resource> res, \
                                 T descriptorConfig, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0 \
                                 ) { \
        const ShaderRegister shaderReg(ResourceDescriptorType:: ## type ##, regSpace, regNum); \
        WINRT_ASSERT(!resMap_.contains(shaderReg)); \
        resMap_.insert({shaderReg, ResourceInfo{.res = res, .bindMethod = method, .descriptorConfig = std::make_shared<T>(descriptorConfig) }}); \
        return *this; \
    } \

    DefineResourceRegisterBindingFunc(SRV)
    DefineResourceRegisterBindingFunc(CBV)
    DefineResourceRegisterBindingFunc(UAV)
    DefineResourceRegisterBindingFunc(Sampler)
    
    DefineResourceRegisterBindingFuncWithConfig(SRV)
    DefineResourceRegisterBindingFuncWithConfig(CBV)
    DefineResourceRegisterBindingFuncWithConfig(UAV)
    DefineResourceRegisterBindingFuncWithConfig(Sampler)
    
#undef DefineResourceRegisterBindingFunc
#undef DefineResourceRegisterBindingFuncWithConfig 
    
    template <typename T>
    ResourceConfiguration& RootConstant(const RootConstantValue<T>& val,
                                          uint16_t regNum,
                                          uint16_t regSpace = 0) {
        const ShaderRegister shaderReg(ResourceDescriptorType::CBV, regSpace, regNum);
        WINRT_ASSERT(!constantMap_.contains(shaderReg));

        const RootConstantInfo decl {
            .data = val.GetData(),
            .num32BitValues = val.GetSizeIn32BitValues()
        };
        constantMap_.insert({shaderReg, decl});
        return *this;
    }

    ResourceConfiguration& Sampler(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        
        const ShaderRegister shaderReg(ResourceDescriptorType::Sampler, regSpace, regNum);
        WINRT_ASSERT(!samplerMap_.contains(shaderReg));

        samplerMap_.insert({shaderReg, desc});
        return *this;
    }
    
    ResourceConfiguration& StaticSampler(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        const ShaderRegister shaderReg(ResourceDescriptorType::Sampler, regSpace, regNum);
        WINRT_ASSERT(!staticSamplerMap_.contains(shaderReg));
        
        staticSamplerMap_.insert({shaderReg, desc});
        return *this;
    }

private:
    friend class PipelineBuilderBase;
    
    PipelineResourceMap<ResourceInfo> resMap_;
    PipelineResourceMap<RootConstantInfo> constantMap_;
    PipelineResourceMap<D3D12_SAMPLER_DESC> samplerMap_;
    PipelineResourceMap<D3D12_SAMPLER_DESC> staticSamplerMap_;
};

class RenderTargetConfiguration {
public:
    RenderTargetConfiguration() = default;

    RenderTargetConfiguration& RenderTarget(ResourceID id, uint16_t slotIndex) {
        WINRT_ASSERT(!renderTargetMap_.contains(slotIndex));
        renderTargetMap_.insert({slotIndex, id});
        return *this;
    }
    
private:
    friend class GraphicsPipelineBuilder;
    std::map<uint16_t, ResourceID> renderTargetMap_;
};

class PipelineBuilderBase {
public:

    virtual ~PipelineBuilderBase() = default;
    PipelineBuilderBase(std::string id) : id_(id) {
        maxResourceConfigurationIndex_ = -1;
    }

#define DefineResourceRegisterBindingFunc(type) \
    void type ## Base(std::weak_ptr<Resource> res, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0) { \
        EnsureValidConfigurationIndex(); \
        const ShaderRegister shaderReg(ResourceDescriptorType:: ## type ##, regSpace, regNum); \
        WINRT_ASSERT(!resMaps_[0].contains(shaderReg)); \
        resMaps_[0].insert({shaderReg, ResourceInfo{.res = res, .bindMethod = method, .descriptorConfig = nullptr}}); \
    } \
    
#define DefineResourceRegisterBindingFuncWithConfig(type) \
    template <IsDescriptorConfiguration T> \
    void type ## Base(std::weak_ptr<Resource> res, \
                                 uint16_t regNum, \
                                 T& descriptorConfig, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0 \
                                 ) { \
        EnsureValidConfigurationIndex(); \
        const ShaderRegister shaderReg(ResourceDescriptorType:: ## type ##, regSpace, regNum); \
        WINRT_ASSERT(!resMaps_[0].contains(shaderReg)); \
        resMaps_[0].insert({shaderReg, ResourceInfo{.res = res, .bindMethod = method, .descriptorConfig = std::make_shared<T>(descriptorConfig) }}); \
    } \

    DefineResourceRegisterBindingFunc(SRV)
    DefineResourceRegisterBindingFunc(CBV)
    DefineResourceRegisterBindingFunc(UAV)
    DefineResourceRegisterBindingFunc(Sampler)
    
    DefineResourceRegisterBindingFuncWithConfig(SRV)
    DefineResourceRegisterBindingFuncWithConfig(CBV)
    DefineResourceRegisterBindingFuncWithConfig(UAV)
    DefineResourceRegisterBindingFuncWithConfig(Sampler)
    
#undef DefineResourceRegisterBindingFunc
#undef DefineResourceRegisterBindingFuncWithConfig 
    
    template <typename T>
    void RootConstantBase(const RootConstantValue<T>& val,
                                          uint16_t regNum,
                                          uint16_t regSpace = 0) {
        EnsureValidConfigurationIndex();
        
        const ShaderRegister shaderReg(ResourceDescriptorType::CBV, regSpace, regNum);
        WINRT_ASSERT(!constantMaps_[0].contains(shaderReg));

        const RootConstantInfo decl {
            .data = val.GetData(),
            .num32BitValues = val.GetSizeIn32BitValues()
        };
        constantMaps_[0].insert({shaderReg, decl});
    }

    void SamplerBase(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        EnsureValidConfigurationIndex();
        
        const ShaderRegister shaderReg(ResourceDescriptorType::Sampler, regSpace, regNum);
        WINRT_ASSERT(!samplerMaps_[0].contains(shaderReg));

        samplerMaps_[0].insert({shaderReg, desc});
    }
    
    void StaticSamplerBase(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        
        EnsureValidConfigurationIndex();
        
        const ShaderRegister shaderReg(ResourceDescriptorType::Sampler, regSpace, regNum);
        WINRT_ASSERT(!staticSamplerMaps_[0].contains(shaderReg));
        
        staticSamplerMaps_[0].insert({shaderReg, desc});
    }

    void ResourceConfigurationBase(uint32_t configIndex, ResourceConfiguration config) {
        WINRT_ASSERT(configIndex == maxResourceConfigurationIndex_ + 1);

        resMaps_.push_back(std::move(config.resMap_));
        constantMaps_.push_back(std::move(config.constantMap_));
        samplerMaps_.push_back(std::move(config.samplerMap_));
        staticSamplerMaps_.push_back(std::move(config.staticSamplerMap_));
        
        maxResourceConfigurationIndex_++;
    }

    virtual std::weak_ptr<PipelineState> Build() = 0;

protected:

    void EnsureValidConfigurationIndex() {
        if(maxResourceConfigurationIndex_ < 0) {
            maxResourceConfigurationIndex_ = 0;
            resMaps_.resize(1);
            constantMaps_.resize(1);
            samplerMaps_.resize(1);
            staticSamplerMaps_.resize(1);
        }
    }
    
    std::string id_;
    
    std::vector<PipelineResourceMap<ResourceInfo>> resMaps_;
    std::vector<PipelineResourceMap<RootConstantInfo>> constantMaps_;
    std::vector<PipelineResourceMap<D3D12_SAMPLER_DESC>> samplerMaps_;
    std::vector<PipelineResourceMap<D3D12_SAMPLER_DESC>> staticSamplerMaps_;


    int maxResourceConfigurationIndex_;
};

class GraphicsPipelineBuilder : public PipelineBuilderBase {
public:
    GraphicsPipelineBuilder& VertexShader(std::string path) { vertexShaderPath_ = path; return *this; }
    GraphicsPipelineBuilder& HullShader(std::string path) { hullShaderPath_ = path; return *this; }
    GraphicsPipelineBuilder& DomainShader(std::string path) { domainShaderPath_ = path; return *this; }
    GraphicsPipelineBuilder& PixelShader(std::string path) { pixelShaderPath_ = path; return *this; }
    GraphicsPipelineBuilder& UseDefaultRenderTarget(uint16_t slotIndex = 0);
    GraphicsPipelineBuilder& UseDefaultDepthBuffer();
    
#define DefineResourceRegisterBindingFunc(type) \
    GraphicsPipelineBuilder& ## type ##(std::weak_ptr<Resource> res, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0) { \
        type ## Base(res, regNum, method, regSpace); \
        return *this; \
    } \
    
#define DefineResourceRegisterBindingFuncWithConfig(type) \
    template <IsDescriptorConfiguration T> \
    GraphicsPipelineBuilder& ## type ##(std::weak_ptr<Resource> res, \
                                 T& descriptorConfig, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0) { \
        type ## Base(res, regNum, method, regSpace, descriptorConfig); \
        return *this; \
    } \

    DefineResourceRegisterBindingFunc(SRV)
    DefineResourceRegisterBindingFunc(CBV)
    DefineResourceRegisterBindingFunc(UAV)
    DefineResourceRegisterBindingFunc(Sampler)
    
    DefineResourceRegisterBindingFuncWithConfig(SRV)
    DefineResourceRegisterBindingFuncWithConfig(CBV)
    DefineResourceRegisterBindingFuncWithConfig(UAV)
    DefineResourceRegisterBindingFuncWithConfig(Sampler)
    
#undef DefineResourceRegisterBindingFunc
#undef DefineResourceRegisterBindingFuncWithConfig 

    template <typename T>
    GraphicsPipelineBuilder& RootConstant(const RootConstantValue<T>& val,
                                          uint16_t regNum,
                                          uint16_t regSpace = 0) {
        RootConstantBase<T>(val, regNum, regSpace);
        return *this;
    }

    GraphicsPipelineBuilder& Sampler(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        Sampler(desc, regNum, regSpace);
        return *this;
    }
    
    GraphicsPipelineBuilder& StaticSampler(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        StaticSamplerBase(desc, regNum, regSpace);
        return *this;
    }

    GraphicsPipelineBuilder& HLSLRootSignaturePriority(ShaderType inType) {
        rootSignaturePriorityShader_ = inType;
        return *this;
    }

    GraphicsPipelineBuilder& VertexBuffer(std::weak_ptr<VertexBufferBase> buffer, uint16_t slotIndex) {
        WINRT_ASSERT(!vertexBufferMap_.contains(slotIndex));
        vertexBufferMap_.insert({slotIndex, buffer});
        return *this;
    }
    
    GraphicsPipelineBuilder& IndexBuffer(std::weak_ptr<IndexBufferBase> buffer) {
        WINRT_ASSERT(indexBuffer_.expired());
        indexBuffer_ = buffer;
        return *this;
    }

    GraphicsPipelineBuilder& RenderTargetConfiguration(uint32_t configIndex, RenderTargetConfiguration config) {
        WINRT_ASSERT(configIndex == curRtConfigIndex_ + 1);
        curRtConfigIndex_++;

        renderTargetMaps_.push_back(config.renderTargetMap_);
        return *this;
    }

    GraphicsPipelineBuilder& DepthBuffer(ResourceID depthBufferId) {
        depthBufferId_ = depthBufferId;
        return *this;
    }

    GraphicsPipelineBuilder& ResourceConfiguration(uint32_t configIndex, ResourceConfiguration config) {
        ResourceConfigurationBase(configIndex, config);
        return *this;
    }

    GraphicsPipelineBuilder& BlendState(D3D12_BLEND_DESC desc) {
        blendDesc_ = desc;
        return *this;
    }

    std::weak_ptr<PipelineState> Build() override {
        return buildFunc_(*this);
    }

private:
    typedef std::function<std::weak_ptr<PipelineState>(const GraphicsPipelineBuilder&)> BuildFunction;
    friend Renderer;
    GraphicsPipelineBuilder(std::string id, BuildFunction buildFunc)
    :
    PipelineBuilderBase(id),
    buildFunc_(buildFunc),
    rootSignaturePriorityShader_(ShaderType::Vertex),
    curRtConfigIndex_(-1)
    {}
        
    BuildFunction buildFunc_;
    
    std::optional<ResourceID> depthBufferId_;
    
    std::optional<std::string> vertexShaderPath_;
    std::optional<std::string> hullShaderPath_;
    std::optional<std::string> domainShaderPath_;
    std::optional<std::string> pixelShaderPath_;

    ShaderType rootSignaturePriorityShader_;

    // std::map needed for sorted order of keys
    std::map<uint16_t, std::weak_ptr<class VertexBufferBase>> vertexBufferMap_;
    
    std::weak_ptr<class IndexBufferBase> indexBuffer_;
    
    // std::map needed for sorted order of keys
    std::vector<std::map<uint16_t, ResourceID>> renderTargetMaps_;
    
    std::optional<D3D12_BLEND_DESC> blendDesc_;

    uint32_t curRtConfigIndex_;
};

class ComputePipelineBuilder : public PipelineBuilderBase {
public:
    ComputePipelineBuilder& ComputeShader(std::string path) { computeShaderPath_ = path; return *this; }
    
#define DefineResourceRegisterBindingFunc(type) \
    ComputePipelineBuilder& ## type ##(std::weak_ptr<Resource> res, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0) { \
        type ## Base(res, regNum, method, regSpace); \
        return *this; \
    } \

#define DefineResourceRegisterBindingFuncWithConfig(type) \
    template <IsDescriptorConfiguration T> \
    ComputePipelineBuilder& ## type ##(std::weak_ptr<Resource> res, \
                                 T descriptorConfig, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0) { \
        type ## Base(res, regNum, method, regSpace, descriptorConfig); \
        return *this; \
    } \
    
    DefineResourceRegisterBindingFunc(SRV)
    DefineResourceRegisterBindingFunc(CBV)
    DefineResourceRegisterBindingFunc(UAV)
    DefineResourceRegisterBindingFunc(Sampler)
    
    DefineResourceRegisterBindingFuncWithConfig(SRV)
    DefineResourceRegisterBindingFuncWithConfig(CBV)
    DefineResourceRegisterBindingFuncWithConfig(UAV)
    DefineResourceRegisterBindingFuncWithConfig(Sampler)
    
#undef DefineResourceRegisterBindingFunc
#undef DefineResourceRegisterBindingFuncWithConfig 
    
    template <typename T>
    ComputePipelineBuilder& RootConstant(RootConstantValue<T>& val,
                                          uint16_t regNum,
                                          uint16_t regSpace = 0) {
        RootConstantBase<T>(val, regNum, regSpace);
        return *this;
    }

    ComputePipelineBuilder& Sampler(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        Sampler(desc, regNum, regSpace);
        return *this;
    }
    
    ComputePipelineBuilder& StaticSampler(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        StaticSamplerBase(desc, regNum, regSpace);
        return *this;
    }
    
    ComputePipelineBuilder& ThreadCount(uint32_t x, uint32_t y, uint32_t z) {
        threadCountX_ = x;
        threadCountY_ = y;
        threadCountZ_ = z;
        return *this;
    }

    ComputePipelineBuilder& ThreadGroupCount(uint32_t x, uint32_t y, uint32_t z) {
        threadGroupCountX_ = x;
        threadGroupCountY_ = y;
        threadGroupCountZ_ = z;
        return *this;
    }

    ComputePipelineBuilder& SyncThreadCountsWithSize(bool maximizeCountX, bool maximizeCountY, bool maximizeCountZ, uint32_t width, uint32_t height, uint32_t depth) {
        // TODO: this number may need to be changed depending on feature levels
        const uint32_t threadCountX = maximizeCountX? 32 : 1;
        const uint32_t threadCountY = maximizeCountY? 32 : 1;
        const uint32_t threadCountZ = maximizeCountZ? 32 : 1;
        
        ThreadCount(threadCountX, threadCountY, threadCountZ);

        ThreadGroupCount(
            (width + threadCountX - 1) / threadCountX,
            (height + threadCountY - 1) / threadCountY,
            (depth + threadCountZ - 1) / threadCountZ
        );

        return *this;
    }
    

    ComputePipelineBuilder& SyncThreadCountsWithTexture2DSize(std::weak_ptr<Texture2D> tex) {
        std::shared_ptr<Texture2D> texShared = tex.lock();
        const uint32_t width = texShared->GetWidth();
        const uint32_t height = texShared->GetHeight();

        SyncThreadCountsWithSize(true, true, false, width, height, 1);
        return *this;
    }
    
    ComputePipelineBuilder& SyncThreadCountsWithTexture3DSize(std::weak_ptr<Texture3D> tex) {
        std::shared_ptr<Texture3D> texShared = tex.lock();
        const uint32_t width = texShared->GetWidth();
        const uint32_t height = texShared->GetHeight();
        const uint32_t depth = texShared->GetDepth();

        SyncThreadCountsWithSize(true, true, false, width, height, depth);
        return *this;
    }
    
    ComputePipelineBuilder& ResourceConfiguration(uint32_t configIndex, ResourceConfiguration config) {
        ResourceConfigurationBase(configIndex, config);
        return *this;
    }
    
    std::weak_ptr<PipelineState> Build() override {
        return buildFunc_(*this);
    }
    
private:
    typedef std::function<std::weak_ptr<PipelineState>(const ComputePipelineBuilder&)> BuildFunction;
    friend Renderer;
    ComputePipelineBuilder(std::string id, BuildFunction buildFunc)
        : PipelineBuilderBase(id), buildFunc_(buildFunc), threadCountX_(0), threadCountY_(0), threadCountZ_(0), threadGroupCountX_(0), threadGroupCountY_(0), threadGroupCountZ_(0) {}
    
    BuildFunction buildFunc_;
    
    std::optional<std::string> computeShaderPath_;
    uint32_t threadCountX_;
    uint32_t threadCountY_;
    uint32_t threadCountZ_;
    
    uint32_t threadGroupCountX_;
    uint32_t threadGroupCountY_;
    uint32_t threadGroupCountZ_;
};


#endif // RENDERER_PIPELINE_BUILDER_H_
