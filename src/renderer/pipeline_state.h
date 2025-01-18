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
        // std::map<ShaderRegister, std::weak_ptr<>
        
    };

    
    virtual ~PipelineState() = default;

    PipelineState(std::string id, PipelineStateType type)
    :
    type_(type),
    future_(promise_.get_future()),
    id_(std::move(id)) 
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
    
    virtual void GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) = 0;

    void OnRootConstantChanged(ShaderRegister shaderReg) {};

private:
    PipelineStateType type_;

    // invalid if not ready (shaders compiling, pso assembling)
    friend PipelineAssembler;
    std::promise<PipelineState::State> promise_;
    std::shared_future<PipelineState::State> future_;
    
    std::string id_;

    friend Renderer;
    std::map<ShaderRegister, ResourceInfo> resMap_;
    std::map<ShaderRegister, RootConstantInfo> constantMap_;
    std::map<ShaderRegister, D3D12_SAMPLER_DESC> samplerMap_;
    std::map<ShaderRegister, D3D12_SAMPLER_DESC> staticSamplerMap_;
};

class GraphicsPipelineState : public PipelineState {
public:
    GraphicsPipelineState(std::string id)
        : PipelineState(id, PipelineStateType::Graphics)
    {}

    void GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) override;

private:
    friend Renderer;
    friend PipelineAssembler;
    std::weak_ptr<VertexShader> vertexShader_;
    std::weak_ptr<HullShader> hullShader_;
    std::weak_ptr<DomainShader> domainShader_;
    std::weak_ptr<PixelShader> pixelShader_;

    
};

class ComputePipelineState : public PipelineState {
public:
    ComputePipelineState(std::string id)
        : PipelineState(id, PipelineStateType::Graphics)
    {}
    
    void GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) override;
    
private:
    friend Renderer;
    friend PipelineAssembler;
    std::weak_ptr<Shader> computeShader_;
};

#endif // RENDERER_PIPELINE_STATE_H_
