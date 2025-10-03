#ifndef RENDERER_PIPELINE_STATE_H_
#define RENDERER_PIPELINE_STATE_H_

#include <future>
#include <optional>
#include <string>
#include "shader_types.h"

class Renderer;
class PipelineAssembler;
class Resource;

class Shader;
class VertexShader;
class HullShader;
class DomainShader;
class PixelShader;
class GeometryShader;
// class ComputeShader;


enum class PipelineStateType {
    Graphics,
    Compute,
    NumPipelineStateTypes,
};

class PipelineState {
public:
    enum StateType {
        Ok,
        CompileError,
        NumStateTypes,
    };
    
    struct State {
        StateType type;
        std::string msg;

        // array of an array of RootParameters,
        // A container of groups of RootParameters, where there
        // are X elements in the container for each resource configuration.
        std::vector<std::vector<std::shared_ptr<RootParameter>>> rootParams;
        
        winrt::com_ptr<ID3D12RootSignature> rootSignature;
        winrt::com_ptr<ID3D12PipelineState> pipelineState;
    };

    
    virtual ~PipelineState() = default;

    PipelineState(std::string id, PipelineStateType type)
    :
    type_(type),
    future_(promise_.get_future()),
    id_(std::move(id)),
    resConfigInd_(0)
    {}

    // are all resources (shaders, pipelines, ) created and ready to go?
    bool IsReadyAndOk() const {
        if(IsStateReady()) {
            PipelineState::State state = future_.get();
            return state.type == PipelineState::StateType::Ok;
        }
        return false;
    }

    bool IsStateReady() const {
        return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    
    const PipelineState::State& GetState_Block() const {
        return future_.get();
    }
    
    virtual void GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) const = 0;

    void OnRootConstantChanged(ShaderRegister shaderReg) {};

    virtual void Execute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList);
    virtual void SetRootSignature(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) = 0;

    bool AreAllResourcesReady() const;
    
    const std::string& GetID() const { return id_; }
    void SetResourceConfigurationIndex(uint32_t ind) {
        WINRT_ASSERT(ind >= 0 && ind < GetNumResourceConfigurations());
        resConfigInd_ = ind;
    }
    uint32_t GetNumResourceConfigurations() const { return resMaps_.size(); }
    
private:
    friend PipelineAssembler;
    virtual std::weak_ptr<Shader> GetShaderForHLSLRootSignatures() const = 0;
    
    PipelineStateType type_;

    // invalid if not ready (shaders compiling, pso assembling)
    std::promise<PipelineState::State> promise_;
    std::shared_future<PipelineState::State> future_;
    
    std::string id_;

    friend Renderer;
    std::vector<PipelineResourceMap<ResourceInfo>> resMaps_;
    std::vector<PipelineResourceMap<RootConstantInfo>> constantMaps_;
    std::vector<PipelineResourceMap<D3D12_SAMPLER_DESC>> samplerMaps_;
    std::vector<PipelineResourceMap<D3D12_SAMPLER_DESC>> staticSamplerMaps_;

    ResourceID depthId_;

    std::optional<D3D12_BLEND_DESC> blendDesc_;

    uint32_t resConfigInd_;
};

class GraphicsPipelineState : public PipelineState {
public:
    GraphicsPipelineState(std::string id)
    : PipelineState(id, PipelineStateType::Graphics),
      rootSignaturePriorityShader_(ShaderType::Vertex),
    numInstances_(1),
    renderTargetConfigInd_(0)
    {}

    void GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) const override;
    std::weak_ptr<Shader> GetShaderForHLSLRootSignatures() const override;
    void SetRootSignature(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) override;

    void Execute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) override;
    void SetNumInstances(uint32_t numInstances) { numInstances_ = numInstances; }

    void InitializeVertexAndIndexBufferDescriptors();

    void SetRenderTargetConfigurationIndex(uint32_t ind) {
        WINRT_ASSERT(ind >= 0 && ind < GetNumRenderTargetConfigurations());
        renderTargetConfigInd_ = ind;
    }
    
    RenderTargetGroupID GetCurrentRenderTargetGroupID() const { return rtGroupIds_[renderTargetConfigInd_]; }
    uint32_t GetNumRenderTargetConfigurations() const { return renderTargetMaps_.size(); }

private:
    friend Renderer;
    friend PipelineAssembler;
    std::weak_ptr<VertexShader> vertexShader_;
    std::weak_ptr<HullShader> hullShader_;
    std::weak_ptr<DomainShader> domainShader_;
    std::weak_ptr<PixelShader> pixelShader_;
    std::weak_ptr<GeometryShader> geometryShader_;
    
    std::map<uint16_t, std::weak_ptr<class VertexBufferBase>> vertexBufferMap_;
    std::weak_ptr<class IndexBufferBase> indexBuffer_;
    
    std::vector<std::map<uint16_t, std::weak_ptr<RenderTargetHandle>>> renderTargetMaps_;
    std::vector<RenderTargetGroupID> rtGroupIds_;

    std::map<uint16_t, DXGI_FORMAT> renderTargetFormats_;
    DXGI_FORMAT depthBufferFormat_;
    DXGI_SAMPLE_DESC sampleDesc_;
    
    std::weak_ptr<class DepthStencilTargetHandle> depthBuffer_;

    std::vector<D3D12_VERTEX_BUFFER_VIEW> vertexBufferDescriptors_;
    std::optional<D3D12_INDEX_BUFFER_VIEW> indexBufferDescriptor_;

    uint32_t numInstances_;
    std::optional<uint32_t> numVertices_;
    
    ShaderType rootSignaturePriorityShader_;
    uint32_t renderTargetConfigInd_;
};

class ComputePipelineState : public PipelineState {
public:
    ComputePipelineState(std::string id)
        : PipelineState(id, PipelineStateType::Compute)
    {}
    
    void GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) const override;
    std::weak_ptr<Shader> GetShaderForHLSLRootSignatures() const override;
    void SetRootSignature(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) override;
    void Execute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) override;
    
private:
    friend Renderer;
    friend PipelineAssembler;
    std::weak_ptr<Shader> computeShader_;
    
    uint32_t threadCountX_;
    uint32_t threadCountY_;
    uint32_t threadCountZ_;
    
    uint32_t threadGroupCountX_;
    uint32_t threadGroupCountY_;
    uint32_t threadGroupCountZ_;
};

#endif // RENDERER_PIPELINE_STATE_H_
