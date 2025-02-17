#ifndef RENDERER_PIPELINE_BUILDER_H_
#define RENDERER_PIPELINE_BUILDER_H_

#include <string>
#include "root_constant_value.h"
#include "renderer_types.h"
#include "shader_types.h"

class PipelineState;
class IndexBufferBase;
class VertexBufferBase;
class Renderer;

class PipelineBuilderBase {
public:

    virtual ~PipelineBuilderBase() = default;
    PipelineBuilderBase(std::string id) : id_(id) {}

#define DefineResourceRegisterBindingFunc(type) \
    void type ## Base(std::weak_ptr<Resource> res, \
                                 uint16_t regNum, \
                                 ResourceBindMethod method = ResourceBindMethod::Automatic, \
                                 uint16_t regSpace = 0) { \
        const ShaderRegister shaderReg(ResourceDescriptorType:: ## type ##, regSpace, regNum); \
        WINRT_ASSERT(!resMap_.contains(shaderReg)); \
        resMap_.insert({shaderReg, ResourceInfo{.res = res, .bindMethod = method}}); \
    } \

    DefineResourceRegisterBindingFunc(SRV)
    DefineResourceRegisterBindingFunc(CBV)
    DefineResourceRegisterBindingFunc(UAV)
    DefineResourceRegisterBindingFunc(Sampler)
    
#undef DefineResourceRegisterBindingFunc
    
    template <typename T>
    void RootConstantBase(const RootConstantValue<T>& val,
                                          uint16_t regNum,
                                          uint16_t regSpace = 0) {
        const ShaderRegister shaderReg(ResourceDescriptorType::CBV, regSpace, regNum);
        WINRT_ASSERT(!constantMap_.contains(shaderReg));

        const RootConstantInfo decl {
            .data = val.GetData(),
            .num32BitValues = val.GetSizeIn32BitValues()
        };
        constantMap_.insert({shaderReg, decl});
    }

    void SamplerBase(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        
        const ShaderRegister shaderReg(ResourceDescriptorType::Sampler, regSpace, regNum);
        WINRT_ASSERT(!samplerMap_.contains(shaderReg));

        samplerMap_.insert({shaderReg, desc});
    }
    
    void StaticSamplerBase(const D3D12_SAMPLER_DESC& desc,
                                     uint16_t regNum,
                                     uint16_t regSpace = 0) {
        const ShaderRegister shaderReg(ResourceDescriptorType::Sampler, regSpace, regNum);
        WINRT_ASSERT(!staticSamplerMap_.contains(shaderReg));
        
        staticSamplerMap_.insert({shaderReg, desc});
    }

    virtual std::weak_ptr<PipelineState> Build() = 0;

protected:
    std::string id_;
    
    std::map<ShaderRegister, ResourceInfo> resMap_;
    std::map<ShaderRegister, RootConstantInfo> constantMap_;
    std::map<ShaderRegister, D3D12_SAMPLER_DESC> samplerMap_;
    std::map<ShaderRegister, D3D12_SAMPLER_DESC> staticSamplerMap_;
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

    DefineResourceRegisterBindingFunc(SRV)
    DefineResourceRegisterBindingFunc(CBV)
    DefineResourceRegisterBindingFunc(UAV)
    DefineResourceRegisterBindingFunc(Sampler)
    
#undef DefineResourceRegisterBindingFunc

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

    GraphicsPipelineBuilder& RenderTarget(ResourceID renderTargetId, uint16_t slotIndex) {
        WINRT_ASSERT(!renderTargetMap_.contains(slotIndex));
        renderTargetMap_.insert({slotIndex, renderTargetId});
        return *this;
    }

    GraphicsPipelineBuilder& DepthBuffer(ResourceID depthBufferId) {
        depthBufferId_ = depthBufferId;
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
    rootSignaturePriorityShader_(ShaderType::Vertex)
    {}
        
    BuildFunction buildFunc_;
    
    std::optional<ResourceID> depthBufferId_;
    
    std::optional<std::string> vertexShaderPath_;
    std::optional<std::string> hullShaderPath_;
    std::optional<std::string> domainShaderPath_;
    std::optional<std::string> pixelShaderPath_;

    ShaderType rootSignaturePriorityShader_;

    std::map<uint16_t, std::weak_ptr<class VertexBufferBase>> vertexBufferMap_;
    std::weak_ptr<class IndexBufferBase> indexBuffer_;
    std::map<uint16_t, ResourceID> renderTargetMap_;
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

    DefineResourceRegisterBindingFunc(SRV)
    DefineResourceRegisterBindingFunc(CBV)
    DefineResourceRegisterBindingFunc(UAV)
    DefineResourceRegisterBindingFunc(Sampler)
    
#undef DefineResourceRegisterBindingFunc
    
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
