#include "pipeline_state.h"
#include "shader.h"

#include <winrt/base.h>

#include "resources.h"


void PipelineState::Execute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    // set root signature
    SetRootSignature(cmdList);

    cmdList->SetPipelineState(GetState_Block().pipelineState.get());
    
    // execute all root parameters
    const auto& rootParams = GetState_Block().rootParams;
    for(auto& rp : rootParams) {
        rp->Execute(cmdList);
    }

}

bool PipelineState::AreAllResourcesReady() const {
    for(const auto& [ignore, resInfo] : resMap_) {
        if(!resInfo.res.lock()->IsReady()) {
            return false;
        }
    }
    return true;
}

#define ADD_IF_OK(var) if(! ## var ##.expired()) { outVec.push_back(var); }

void GraphicsPipelineState::GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) const {
    WINRT_ASSERT(!vertexShader_.expired() && !pixelShader_.expired());

    ADD_IF_OK(vertexShader_);
    ADD_IF_OK(hullShader_);
    ADD_IF_OK(domainShader_);
    ADD_IF_OK(pixelShader_);
}

void ComputePipelineState::GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) const {
    ADD_IF_OK(computeShader_);
}
#undef ADD_IF_OK

std::weak_ptr<Shader> GraphicsPipelineState::GetShaderForHLSLRootSignatures() const {
    switch(rootSignaturePriorityShader_) {
    case ShaderType::Vertex:
        return vertexShader_;
    case ShaderType::Hull:
        return hullShader_;
    case ShaderType::Domain:
        return domainShader_;
    case ShaderType::Pixel:
        return pixelShader_;
    default:
        return std::weak_ptr<Shader>();
    }
}

void GraphicsPipelineState::SetRootSignature(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    cmdList->SetGraphicsRootSignature(GetState_Block().rootSignature.get());
}

void GraphicsPipelineState::Execute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    // handles root parameters
    PipelineState::Execute(cmdList);

    // input assembly
    const bool usingIndexBuffer = indexBufferDescriptor_.has_value();

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // TODO: custom, perhaps controlled by VertexBuffer
    cmdList->IASetVertexBuffers(0, vertexBufferDescriptors_.size(), vertexBufferDescriptors_.data());
    if(usingIndexBuffer) {
        cmdList->IASetIndexBuffer(&indexBufferDescriptor_.value());
    }

    if(usingIndexBuffer) {
        const uint32_t numIndices = indexBuffer_.lock()->GetNumIndices();
        cmdList->DrawIndexedInstanced(numIndices, numInstances_, 0, 0, 0);
    }
    else {
        WINRT_ASSERT(numVertices_.has_value());
        cmdList->DrawInstanced(numVertices_.value(), numInstances_, 0, 0);
    }
}

std::weak_ptr<Shader> ComputePipelineState::GetShaderForHLSLRootSignatures() const {
    return computeShader_;
}

void ComputePipelineState::SetRootSignature(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    cmdList->SetComputeRootSignature(GetState_Block().rootSignature.get());
}

void ComputePipelineState::Execute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    // handles root parameters
    PipelineState::Execute(cmdList);

    // dispatch
    cmdList->Dispatch(threadGroupCountX_, threadGroupCountY_, threadGroupCountZ_);
}
