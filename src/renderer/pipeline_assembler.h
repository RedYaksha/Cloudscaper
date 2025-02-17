#ifndef RENDERER_PIPELINE_ASSEMBLER_H_
#define RENDERER_PIPELINE_ASSEMBLER_H_
#include <d3d12.h>
#include <memory>

#include "pipeline_state.h"
#include "shader_types.h"
#include "multithreading/thread_pool.h"

class DescriptorAllocator;
class DescriptorHeapAllocation;

class PipelineAssembler {
public:
    PipelineAssembler(winrt::com_ptr<ID3D12Device> device,
                    const std::weak_ptr<DescriptorAllocator>& resourceDescriptorAllocator,
                    const std::weak_ptr<DescriptorAllocator>& samplerDescriptorAllocator);
    ~PipelineAssembler();

    bool Enqueue(std::weak_ptr<PipelineState> pso);
    void Flush();

private:
    PipelineState::State AssemblePipeline(std::weak_ptr<PipelineState> pso, std::promise<PipelineState::State>& statePromise);
    void AssembleGraphicsPipeline(std::weak_ptr<GraphicsPipelineState> pso, std::promise<PipelineState::State>& statePromise);
    void AssembleComputePipeline(std::weak_ptr<ComputePipelineState> pso, std::promise<PipelineState::State>& statePromise);

    std::vector<D3D12_INPUT_ELEMENT_DESC> CreateGraphicsInputLayoutDesc(std::shared_ptr<GraphicsPipelineState> pso);

    winrt::com_ptr<ID3D12PipelineState> CreateD3DGraphicsPipeline(std::shared_ptr<GraphicsPipelineState> pso,
                                                                  ID3D12RootSignature* rootSig,
                                                                  D3D12_INPUT_LAYOUT_DESC inputLayout);
    
    winrt::com_ptr<ID3D12PipelineState> CreateD3DComputePipeline(std::shared_ptr<ComputePipelineState> pso,
                                                                 ID3D12RootSignature* rootSignature);
    
    void InitializeVertexAndIndexBuffers(std::shared_ptr<GraphicsPipelineState> pso);
    
    
    
    winrt::com_ptr<ID3D12Device> device_;
    std::weak_ptr<DescriptorAllocator> resourceDescriptorAllocator_; 
    std::weak_ptr<DescriptorAllocator> samplerDescriptorAllocator_; 
    std::shared_ptr<ThreadPool<PipelineState::State>> threadPool_;
    std::queue<std::weak_ptr<PipelineState>> queue_;
};

#endif // RENDERER_PIPELINE_ASSEMBLER_H_
