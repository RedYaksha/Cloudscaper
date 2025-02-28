#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>

#include "pipeline_assembler.h"

#include <comdef.h>

#include "resources.h"
#include "shader.h"
#include "memory/descriptor_allocator.h"
#include <ranges>
#include <iostream>


struct DescriptorAllocationInfo {
    std::weak_ptr<DescriptorHeapAllocation> allocation;
    
    // the offset from the start of the allocation
    // e.g. if allocation represents alloc=[t4, t5, t6, u0, u1, u2],
    //      then,
    //      { t5, DescriptorAllocationInfo{ alloc, 1 } }
    //      { u1, DescriptorAllocationInfo{ alloc, 4 } }
    //      would be in the map
    uint16_t offsetFromAllocBase;
};

typedef PipelineResourceMap<DescriptorAllocationInfo> RegisterToDescriptorAllocationMap;

struct DescriptorTableDescription {
    uint32_t paramIndex;
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;

    uint32_t GetTotalDescriptors() const {
        uint32_t sum = 0;
        for(const auto& r : ranges) {
            sum += r.NumDescriptors;
        }
        return sum;
    }

    std::weak_ptr<DescriptorHeapAllocation> allocation;
};

// info for creating D3D12_DESCRIPTOR_RANGE
struct DescriptorRangeDescription {
    ResourceDescriptorType descriptorType;
    uint16_t registerSpace;
    uint16_t baseRegister;
    uint16_t numDescriptors;
};

namespace {
    
    const std::set<D3D12_DESCRIPTOR_RANGE_TYPE> resourceRangeTypes = {
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
        D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
    };
    
    const std::set<D3D12_DESCRIPTOR_RANGE_TYPE> samplerRangeTypes = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER };

    RootParameterUsageMap GetMergedRootParameterUsageMap(std::vector<std::weak_ptr<Shader>> shaders);
    
    void ExtractAllRootSignatureDescriptorTablesWithType(const D3D12_ROOT_SIGNATURE_DESC* rootSigDesc,
                                                         const std::set<D3D12_DESCRIPTOR_RANGE_TYPE>& allowList,
                                                         std::vector<DescriptorTableDescription>& outDescriptorTables);
    
    // returns num allocations made
    uint32_t CreateDescriptorAllocationsFromTables(std::shared_ptr<DescriptorAllocator>& descriptorAllocator,
                                                   std::vector<DescriptorTableDescription>& tables,
                                                   RegisterToDescriptorAllocationMap& outAllocationMap);
    
    bool InitializeDescriptorAllocations(winrt::com_ptr<ID3D12Device> device,
                                         const std::vector<PipelineResourceMap<ResourceInfo>>& resMapArr,
                                         const std::vector<PipelineResourceMap<D3D12_SAMPLER_DESC>>& samplerMapArr,
                                         const RegisterToDescriptorAllocationMap& allocations,
                                         const uint32_t resolutionConfigIndex);
    
    bool InitializeDescriptorTableRootParameters(const std::vector<DescriptorTableDescription>& tables,
                                                 const bool& isCompute,
                                                 std::vector<std::shared_ptr<RootParameter>>& outRootParameters);

    bool InitializeNonDescriptorTableRootParametersFromRootSignature(const D3D12_ROOT_SIGNATURE_DESC* rootSigDesc,
                                                                     const bool& isCompute,
                                                                     const std::vector<PipelineResourceMap<ResourceInfo>>& resMapArr,
                                                                     const std::vector<PipelineResourceMap<RootConstantInfo>>& constantMapArr,
                                                                     const uint32_t resConfigIndex,
                                                                     std::vector<std::shared_ptr<RootParameter>>& outRootParameters);

    bool ComputeDescriptorRangesFromContinuousResources(const RootParameterUsageMap& usageMap,
                                                        std::vector<DescriptorRangeDescription>& outRanges);

    void InitializeDescriptorRanges(const std::vector<DescriptorRangeDescription>& ranges,
                                    const std::set<D3D12_DESCRIPTOR_RANGE_TYPE>& allowList,
                                    std::vector<D3D12_DESCRIPTOR_RANGE1>& outRanges);

    void GenerateRootSignature(winrt::com_ptr<ID3D12Device> device,
                               const std::vector<std::weak_ptr<Shader>>& shaders,
                               const PipelineResourceMap<ResourceInfo>& resMap,
                               const PipelineResourceMap<RootConstantInfo>& constantMap,
                               const PipelineResourceMap<D3D12_SAMPLER_DESC>& samplerMap,
                               const PipelineResourceMap<D3D12_SAMPLER_DESC>& staticSamplerMap,
                               winrt::com_ptr<ID3DBlob>& outRSBlob,
                               winrt::com_ptr<ID3D12RootSignature>& outRS);

} // pipeline_assembler_utils

PipelineAssembler::PipelineAssembler(winrt::com_ptr<ID3D12Device> device,
                    const std::weak_ptr<DescriptorAllocator>& resourceDescriptorAllocator,
                    const std::weak_ptr<DescriptorAllocator>& samplerDescriptorAllocator) 
:
device_(device), 
resourceDescriptorAllocator_(resourceDescriptorAllocator),
samplerDescriptorAllocator_(samplerDescriptorAllocator)
{
    threadPool_ = std::make_shared<ThreadPool<PipelineState::State>>();
    threadPool_->Start();
}

PipelineAssembler::~PipelineAssembler() {
    std::cout << "Destroying pipeline assembler..." << std::endl;
    
}

void PipelineAssembler::Flush() {
    while(queue_.size() > 0) {
        std::weak_ptr<PipelineState> pso = std::move(queue_.front());
        queue_.pop();

        std::promise<PipelineState::State>& statePromise = pso.lock()->promise_;

        std::packaged_task<PipelineState::State()> task(std::bind(&PipelineAssembler::AssemblePipeline, this, pso, std::ref(statePromise)));
        // pso.lock()->future_ = task.get_future();

        std::cout << "Adding build PSO task: " << pso.lock()->GetID() << std::endl;
        threadPool_->AddTask(std::move(task));
    }
}

PipelineState::State PipelineAssembler::AssemblePipeline(std::weak_ptr<PipelineState> inPso, std::promise<PipelineState::State>& statePromise) {
    std::shared_ptr<PipelineState> pso = inPso.lock();

    std::cout << "AssemblePipeline() : " << pso->GetID() << std::endl;

    std::vector<std::weak_ptr<Shader>> shaders;
    pso->GetShaders(shaders);

    // wait for every shader to finish compiling
    for(const auto& s : shaders) {
        std::shared_ptr<Shader> shader = s.lock();
        std::cout << std::format("Waiting for {}", shader->GetSourceFile()) << std::endl;

        const Shader::State& shaderState = shader->GetState_Block();
        
        if(shaderState.type != Shader::StateType::Ok) {
            PipelineState::State out;
            out.type = PipelineState::StateType::CompileError;
            out.msg = std::format("Shader ({}) failed to compile. Pipeline assembly failed. {}", s.lock()->GetSourceFile(), pso->id_); 
            std::cout << out.msg << std::endl;
            std::cout << "Error message: " << shaderState.msg << std::endl;
            
            statePromise.set_value(out);
            return out;
        }
        
        std::cout << "Success!" << shader->GetSourceFile() << std::endl;
    }

    HRESULT hr;

    const std::weak_ptr<Shader> rootSigShader = pso->GetShaderForHLSLRootSignatures();
    WINRT_ASSERT(!rootSigShader.expired());
    WINRT_ASSERT(rootSigShader.lock()->IsStateReady());
    
    const std::shared_ptr<Shader::CompilationData> compileData = rootSigShader.lock()->GetState_Block().compileData;

    winrt::com_ptr<ID3DBlob> rsBlob;
    winrt::com_ptr<ID3D12RootSignature> rootSig;
    LPVOID rootSigPtr;
    SIZE_T rootSigSize;

    if(compileData->rootSigBlob) {
        rootSigPtr = compileData->rootSigBlob->GetBufferPointer();
        rootSigSize = compileData->rootSigBlob->GetBufferSize();
        
        hr = device_->CreateRootSignature(0,
            compileData->rootSigBlob->GetBufferPointer(),
            compileData->rootSigBlob->GetBufferSize(),
            __uuidof(ID3D12RootSignature),
            rootSig.put_void());
        winrt::check_hresult(hr);
    }
    else {
        GenerateRootSignature(device_,
                              shaders,
                              pso->resMaps_[0],
                              pso->constantMaps_[0],
                              pso->samplerMaps_[0],
                              pso->staticSamplerMaps_[0],
                              rsBlob,
                              rootSig);
        
        rootSigPtr = rsBlob->GetBufferPointer();
        rootSigSize = rsBlob->GetBufferSize();
    }

    winrt::com_ptr<ID3D12RootSignatureDeserializer> deserializer;
    hr = D3D12CreateRootSignatureDeserializer(
            rootSigPtr,
            rootSigSize,
            __uuidof(ID3D12RootSignatureDeserializer),
            deserializer.put_void());
    
    winrt::check_hresult(hr);
    
    const D3D12_ROOT_SIGNATURE_DESC* rootSigDesc = deserializer->GetRootSignatureDesc();

    // get all descriptor tables
    std::shared_ptr<DescriptorAllocator> resDescriptorAllocator = resourceDescriptorAllocator_.lock();
    std::shared_ptr<DescriptorAllocator> samplerDescriptorAllocator = samplerDescriptorAllocator_.lock();
    
    const bool isCompute = pso->type_ == PipelineStateType::Compute;
    std::vector<std::vector<std::shared_ptr<RootParameter>>> rootParametersArr;
    rootParametersArr.resize(pso->GetNumResourceConfigurations());
    
    for(int curConfigIndex = 0 ; curConfigIndex < pso->GetNumResourceConfigurations(); curConfigIndex++) {
        RegisterToDescriptorAllocationMap allocationMap;
        std::vector<DescriptorTableDescription> descriptorTables;
        
        //
        // resource descriptor tables
        //
        ::ExtractAllRootSignatureDescriptorTablesWithType(rootSigDesc,
                                                          ::resourceRangeTypes,
                                                          descriptorTables);
        
        // allocate descriptors for all descriptor tables
        ::CreateDescriptorAllocationsFromTables(resDescriptorAllocator,
                                                descriptorTables,
                                                allocationMap);
        
        //
        // sampler descriptor tables
        //
        ::ExtractAllRootSignatureDescriptorTablesWithType(rootSigDesc,
                                                          ::samplerRangeTypes,
                                                          descriptorTables);
        

        // allocate descriptors for all descriptor tables
        ::CreateDescriptorAllocationsFromTables(samplerDescriptorAllocator,
                                                descriptorTables,
                                                allocationMap);
        
        // init descriptors, according to linked resources
        bool success = ::InitializeDescriptorAllocations(device_,
                                                         pso->resMaps_,
                                                         pso->samplerMaps_,
                                                         allocationMap,
                                                         curConfigIndex);
        WINRT_ASSERT(success);
        
        // create RootParameters so pipeline knows what to bind at render time
        std::vector<std::shared_ptr<RootParameter>>& rootParams = rootParametersArr[curConfigIndex];
        
        ::InitializeDescriptorTableRootParameters(descriptorTables,
                                                  isCompute,
                                                  rootParams);

        // create all the other RootParameters that are non-descriptor table
        ::InitializeNonDescriptorTableRootParametersFromRootSignature(rootSigDesc,
                                                                      isCompute,
                                                                      pso->resMaps_,
                                                                      pso->constantMaps_,
                                                                      curConfigIndex,
                                                                      rootParams);
    }

    winrt::com_ptr<ID3D12PipelineState> pipeline;
    if(isCompute) {
        pipeline = CreateD3DComputePipeline(std::static_pointer_cast<ComputePipelineState>(pso), rootSig.get());
    }
    else {
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElems = CreateGraphicsInputLayoutDesc(std::static_pointer_cast<GraphicsPipelineState>(pso));
        
        D3D12_INPUT_LAYOUT_DESC inputLayoutDesc;
        inputLayoutDesc.NumElements = inputElems.size();
        inputLayoutDesc.pInputElementDescs = inputElems.data();
        
        pipeline = CreateD3DGraphicsPipeline(std::static_pointer_cast<GraphicsPipelineState>(pso),
                                             rootSig.get(),
                                             inputLayoutDesc);

        InitializeVertexAndIndexBuffers(std::static_pointer_cast<GraphicsPipelineState>(pso));
    }
    
    WINRT_ASSERT(pipeline);

    std::wstring psoID(pso->GetID().begin(), pso->GetID().end());
    hr = pipeline->SetName(psoID.c_str());
    WINRT_ASSERT(SUCCEEDED(hr));

    PipelineState::State out;
    out.type = PipelineState::StateType::Ok;
    out.msg = "";
    out.rootParams = std::move(rootParametersArr);
    out.rootSignature = rootSig;
    out.pipelineState = pipeline;
    statePromise.set_value(out);
    
    return out;
}

std::vector<D3D12_INPUT_ELEMENT_DESC> PipelineAssembler::CreateGraphicsInputLayoutDesc(std::shared_ptr<GraphicsPipelineState> pso) {
    WINRT_ASSERT(!pso->vertexShader_.expired());

    // TODO
    static std::unordered_map<ShaderDataType, DXGI_FORMAT> shaderTypeToFormat = {
        {ShaderDataType::Float, DXGI_FORMAT_R32_FLOAT},
        {ShaderDataType::Float2, DXGI_FORMAT_R32G32_FLOAT},
        {ShaderDataType::Float3, DXGI_FORMAT_R32G32B32_FLOAT},
        {ShaderDataType::Float4, DXGI_FORMAT_R32G32B32A32_FLOAT},
        {ShaderDataType::Int, DXGI_FORMAT_R32_SINT},
        {ShaderDataType::Int2, DXGI_FORMAT_R32G32_SINT},
        {ShaderDataType::Int3, DXGI_FORMAT_R32G32B32_SINT},
        {ShaderDataType::Int4, DXGI_FORMAT_R32G32B32A32_SINT},
        {ShaderDataType::UInt, DXGI_FORMAT_R32_UINT},
        {ShaderDataType::UInt2, DXGI_FORMAT_R32G32_UINT},
        {ShaderDataType::UInt3, DXGI_FORMAT_R32G32B32_UINT},
        {ShaderDataType::UInt4, DXGI_FORMAT_R32G32B32A32_UINT},
    };

    const auto& inputLayoutElems = pso->vertexShader_.lock()->GetState_Block().compileData->inputLayoutElems;
    std::vector<D3D12_INPUT_ELEMENT_DESC> elems;
    uint16_t curSlot = 0;
    for(auto it = pso->vertexBufferMap_.begin(); it != pso->vertexBufferMap_.end(); ++it) {
        WINRT_ASSERT(it->first == curSlot);

        const VertexBufferLayout& layout = it->second.lock()->GetLayout();
        const VertexBufferUsage usage = it->second.lock()->GetUsage();

        for(int i = 0; i < layout.elements.size(); i++) {
            const auto& elem = layout.elements[i];

            WINRT_ASSERT(shaderTypeToFormat.contains(elem.dataType));
            
            D3D12_INPUT_ELEMENT_DESC newElementDesc;
            newElementDesc.SemanticName = elem.semanticName.c_str();
            newElementDesc.SemanticIndex = 0; // TODO: semantic index is to distinguish those with same SemanticNames...
            newElementDesc.Format = shaderTypeToFormat.at(elem.dataType);
            newElementDesc.InputSlot = curSlot;
            newElementDesc.AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

            // AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT is relative to the same input slot.
            // if using different slots, then we must explicitly set the offset to 0
            if(i == 0) {
                newElementDesc.AlignedByteOffset = 0;
            }
            
            if(usage == VertexBufferUsage::PerInstance) {
                newElementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                newElementDesc.InstanceDataStepRate = 1;
            }
            else if(usage == VertexBufferUsage::PerVertex) {
                newElementDesc.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                newElementDesc.InstanceDataStepRate = 0;
            }

            elems.push_back(newElementDesc);
        }

        curSlot++;
    }

    return elems;
}

winrt::com_ptr<ID3D12PipelineState> PipelineAssembler::CreateD3DGraphicsPipeline(std::shared_ptr<GraphicsPipelineState> pso,
                                                                                 ID3D12RootSignature* rootSig,
                                                                                 D3D12_INPUT_LAYOUT_DESC inputLayout) {
    
    auto shaderToBytecode = [](std::weak_ptr<Shader> shader) -> CD3DX12_SHADER_BYTECODE {
        if(shader.expired()) {
            return CD3DX12_SHADER_BYTECODE(NULL, 0u);
        }
        
        winrt::com_ptr<IDxcBlob> blob = shader.lock()->GetState_Block().compileData->shaderBlob;
        return CD3DX12_SHADER_BYTECODE(blob->GetBufferPointer(), blob->GetBufferSize());
    };

    WINRT_ASSERT(pso->renderTargetMap_.size() > 0);

    auto getRenderTargetFormat = [&](uint16_t slotIndex)->DXGI_FORMAT {
        if(pso->renderTargetMap_.contains(slotIndex)) {
            return pso->renderTargetMap_.at(slotIndex).lock()->format;
        }
        return DXGI_FORMAT_UNKNOWN;
    };

    const uint32_t numRenderTargets = pso->renderTargetMap_.size();

    DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_UNKNOWN;
    if(!pso->depthBuffer_.expired()) {
        depthBufferFormat = pso->depthBuffer_.lock()->format;
    }

    const DXGI_SAMPLE_DESC firstSampleDesc = pso->renderTargetMap_.begin()->second.lock()->sampleDesc;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
    desc.pRootSignature = rootSig;
    desc.VS = shaderToBytecode(pso->vertexShader_);
    desc.PS = shaderToBytecode(pso->pixelShader_);
    desc.DS = shaderToBytecode(pso->domainShader_);
    desc.HS = shaderToBytecode(pso->hullShader_);
    desc.GS = shaderToBytecode(pso->geometryShader_);
    desc.StreamOutput = D3D12_STREAM_OUTPUT_DESC {
                            .pSODeclaration = NULL,
                            .NumEntries = 0,
                            .pBufferStrides = NULL,
                            .NumStrides = 0,
                            .RasterizedStream = 0 
                        };

    // TODO: configure settings (e.g. cull mode, blend state, etc.)
    desc.BlendState = pso->blendDesc_.value_or( CD3DX12_BLEND_DESC(D3D12_DEFAULT) );

    
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RasterizerState.FrontCounterClockwise = TRUE;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    if(pso->depthBuffer_.expired()) {
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;
    }
    desc.InputLayout = inputLayout;
    desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // TODO
    desc.NumRenderTargets = numRenderTargets; // TODO
    desc.RTVFormats[0] = getRenderTargetFormat(0);
    desc.RTVFormats[1] = getRenderTargetFormat(1);
    desc.RTVFormats[2] = getRenderTargetFormat(2);
    desc.RTVFormats[3] = getRenderTargetFormat(3);
    desc.RTVFormats[4] = getRenderTargetFormat(4);
    desc.RTVFormats[5] = getRenderTargetFormat(5);
    desc.RTVFormats[6] = getRenderTargetFormat(6);
    desc.RTVFormats[7] = getRenderTargetFormat(7);
    
    desc.DSVFormat = depthBufferFormat;
    desc.SampleDesc = firstSampleDesc;
    
    desc.NodeMask = 0;
    desc.CachedPSO = D3D12_CACHED_PIPELINE_STATE {
                    .pCachedBlob = NULL,
                    .CachedBlobSizeInBytes = 0 
                };
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    winrt::com_ptr<ID3D12PipelineState> pipeline;
    device_->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), pipeline.put_void());

    return pipeline;
}

winrt::com_ptr<ID3D12PipelineState> PipelineAssembler::CreateD3DComputePipeline(std::shared_ptr<ComputePipelineState> pso,
                                                                                ID3D12RootSignature* rootSig) {

    WINRT_ASSERT(!pso->computeShader_.expired());

    winrt::com_ptr<IDxcBlob> csBlob = pso->computeShader_.lock()->GetState_Block().compileData->shaderBlob;
    CD3DX12_SHADER_BYTECODE bytecode(csBlob->GetBufferPointer(), csBlob->GetBufferSize());
    
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc {
        .pRootSignature = rootSig,
        .CS = bytecode,
        .NodeMask = 0,
        .CachedPSO = D3D12_CACHED_PIPELINE_STATE {
                        .pCachedBlob = NULL,
                        .CachedBlobSizeInBytes = 0
                    },
        .Flags = D3D12_PIPELINE_STATE_FLAG_NONE
    };

    winrt::com_ptr<ID3D12PipelineState> pipeline;
    device_->CreateComputePipelineState(&desc, __uuidof(ID3D12PipelineState), pipeline.put_void());
    return pipeline;
}

void PipelineAssembler::InitializeVertexAndIndexBuffers(std::shared_ptr<GraphicsPipelineState> pso) {
    pso->InitializeVertexAndIndexBufferDescriptors();
}

bool PipelineAssembler::Enqueue(std::weak_ptr<PipelineState> pso) {
    std::cout << "Enqueuing " + pso.lock()->id_ << std::endl;
    queue_.push(std::move(pso));
    return true;
}


namespace {
    RootParameterUsageMap GetMergedRootParameterUsageMap(std::vector<std::weak_ptr<Shader>> shaders) {
        RootParameterUsageMap outMap;
        if(shaders.size() <= 0) {
            return outMap;
        }
        // extract all usage maps
        WINRT_ASSERT(shaders.size() > 0);

        outMap = shaders[0].lock()->GetState_Block().compileData->rootParamUsage;

        for(int i = 1; i < shaders.size(); i++) {

            // for intersecting keys, merge the underlying set of register numbers
            // otherwise, just copy to outMap
            for(auto [key, val] : shaders[i].lock()->GetState_Block().compileData->rootParamUsage) {
                
                if(outMap.contains(key)) {
                    outMap[key].insert(val.begin(), val.end());
                }
                else {
                    outMap.insert({key, val});
                }
                
            }
            
        }

        return outMap;
    }

    void ExtractAllRootSignatureDescriptorTablesWithType(const D3D12_ROOT_SIGNATURE_DESC* rootSigDesc,
                                                         const std::set<D3D12_DESCRIPTOR_RANGE_TYPE>& allowList,
                                                         std::vector<DescriptorTableDescription>& outDescriptorTables) {
        for(int i = 0; i < rootSigDesc->NumParameters; i++) {
            const D3D12_ROOT_PARAMETER rootParam = rootSigDesc->pParameters[i];

            if(rootParam.ParameterType != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                continue;
            }

            
            DescriptorTableDescription descriptorTable;
            descriptorTable.paramIndex = i;

            // drInd <==> descriptor range index
            for(int drInd = 0; drInd < rootParam.DescriptorTable.NumDescriptorRanges; drInd++) {
                const D3D12_DESCRIPTOR_RANGE drange = rootParam.DescriptorTable.pDescriptorRanges[drInd];
                if(!allowList.contains(drange.RangeType)) {
                    continue;
                }

                descriptorTable.ranges.push_back(drange);
            }

            if(!descriptorTable.ranges.empty()) {
                outDescriptorTables.push_back(descriptorTable);
            }
        }
        
    }

    uint32_t CreateDescriptorAllocationsFromTables(std::shared_ptr<DescriptorAllocator>& descriptorAllocator,
                                                   std::vector<DescriptorTableDescription>& tables,
                                                   RegisterToDescriptorAllocationMap& outAllocationMap) {
        
        static const std::map<D3D12_DESCRIPTOR_RANGE_TYPE, ResourceDescriptorType> typeMap = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, ResourceDescriptorType::SRV},
            {D3D12_DESCRIPTOR_RANGE_TYPE_CBV, ResourceDescriptorType::CBV},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, ResourceDescriptorType::UAV},
            {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, ResourceDescriptorType::Sampler},
        };

        uint32_t numAllocations = 0;
        
        for(DescriptorTableDescription& table : tables) {
            // if the allocation has already been made, skip
            if(!table.allocation.expired()) {
                continue;
            }
            
            // 1 allocation per table
            std::weak_ptr<DescriptorHeapAllocation> allocation = descriptorAllocator->Allocate(table.GetTotalDescriptors());
            WINRT_ASSERT(!allocation.expired() && "Descriptor allocation failed.");

            // 
            numAllocations++;
            table.allocation = allocation;


            // per range, map a ShaderRegister so bound resources can be linked to a spot in the
            // descriptor allocation
            uint16_t offset = 0;
            for(const D3D12_DESCRIPTOR_RANGE& range : table.ranges) {
                const ResourceDescriptorType descriptorType = typeMap.at(range.RangeType);
                
                for(uint16_t i = 0; i < range.NumDescriptors; i++) {
                    ShaderRegister shaderReg(descriptorType, range.RegisterSpace, range.BaseShaderRegister + i);

                    outAllocationMap.insert({
                        std::move(shaderReg),
                        DescriptorAllocationInfo{
                            .allocation= allocation,
                            .offsetFromAllocBase = offset // needed for referring back to where in the allocation a resource is
                        }
                    });

                    offset++;
                }
            }
            
        }

        return numAllocations;
    }

    bool InitializeDescriptorAllocations(winrt::com_ptr<ID3D12Device> device,
                                         const std::vector<PipelineResourceMap<ResourceInfo>>& resMapArr,
                                         const std::vector<PipelineResourceMap<D3D12_SAMPLER_DESC>>& samplerMapArr,
                                         const RegisterToDescriptorAllocationMap& allocations,
                                         const uint32_t resolutionConfigIndex) {
        WINRT_ASSERT(device);
                                         
        for(const auto& [shaderReg, val] : allocations) {
            const std::shared_ptr<DescriptorHeapAllocation> allocation = val.allocation.lock();
            
            const uint32_t offset = val.offsetFromAllocBase;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
            bool success = allocation->GetCPUDescriptorHandleOffsetted(offset, cpuHandle);
            WINRT_ASSERT(success);

            // find resource or sampler
            bool foundShaderRegister = false;

            ResourceInfo resInfo;
            for(int i = resolutionConfigIndex; i >= 0 && !foundShaderRegister ; i--) {
                const PipelineResourceMap<ResourceInfo>& resMap = resMapArr[i];

                if(resMap.contains(shaderReg)) {
                    // find correct location in allocation
                    resInfo = resMap.at(shaderReg);
                    
                    WINRT_ASSERT(resInfo.bindMethod == ResourceBindMethod::Automatic ||
                                 resInfo.bindMethod == ResourceBindMethod::DescriptorTable);

                    WINRT_ASSERT(!resInfo.res.expired());
                    std::shared_ptr<const Resource> res = resInfo.res.lock();

                    success = res->CreateDescriptorByResourceType(cpuHandle, shaderReg.type, resInfo.descriptorConfig);
                    WINRT_ASSERT(success);

                    foundShaderRegister = true;
                }
            }
            
            for(int i = resolutionConfigIndex; i >= 0 && !foundShaderRegister ; i--) {
                const PipelineResourceMap<D3D12_SAMPLER_DESC>& samplerMap = samplerMapArr[i];
                if(samplerMap.contains(shaderReg)) {
                    const D3D12_SAMPLER_DESC& samplerDesc = samplerMap.at(shaderReg);
                    device->CreateSampler(&samplerDesc, cpuHandle);
                    
                    foundShaderRegister = true;
                }
            }
            
            if(!foundShaderRegister) {
                std::cout << std::format("Failed to find shader register! ({},{},{})",
                    (uint8_t) shaderReg.type, shaderReg.regSpace, shaderReg.regNumber) << std::endl;
                return false;
            }
        }

        return true;
    }

    bool InitializeDescriptorTableRootParameters(const std::vector<DescriptorTableDescription>& tables,
                                                 const bool& isCompute,
                                                 std::vector<std::shared_ptr<RootParameter>>& outRootParameters) {
        
        std::set<DescriptorAllocationInfo> allocationsHandled;

        for(const DescriptorTableDescription& table : tables) {
            const std::shared_ptr<DescriptorHeapAllocation> allocation = table.allocation.lock();
            D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
            const bool success = allocation->GetGPUDescriptorHandle(gpuHandle);
            WINRT_ASSERT(success);
            
            const auto newParam = std::make_shared<DescriptorTableParameter>(table.paramIndex, isCompute, gpuHandle);
            outRootParameters.push_back(std::move(newParam));
        }

        return true;
    }
    
    bool InitializeNonDescriptorTableRootParametersFromRootSignature(const D3D12_ROOT_SIGNATURE_DESC* rootSigDesc,
                                                                     const bool& isCompute,
                                                                     const std::vector<PipelineResourceMap<ResourceInfo>>& resMapArr,
                                                                     const std::vector<PipelineResourceMap<RootConstantInfo>>& constantMapArr,
                                                                     const uint32_t resConfigIndex,
                                                                     std::vector<std::shared_ptr<RootParameter>>& outRootParameters) {
        
        static const std::map<D3D12_ROOT_PARAMETER_TYPE, ResourceDescriptorType> paramToResourceType = {
            {D3D12_ROOT_PARAMETER_TYPE_SRV, ResourceDescriptorType::SRV},
            {D3D12_ROOT_PARAMETER_TYPE_CBV, ResourceDescriptorType::CBV},
            {D3D12_ROOT_PARAMETER_TYPE_UAV, ResourceDescriptorType::UAV},
        };

        
        for(int i = 0; i < rootSigDesc->NumParameters; i++) {
            const D3D12_ROOT_PARAMETER rootParam = rootSigDesc->pParameters[i];

            if(rootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
                continue;
            }

            if(rootParam.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {

                bool found = false;
                
                const ShaderRegister shaderReg(ResourceDescriptorType::CBV,
                                         rootParam.Constants.RegisterSpace,
                                         rootParam.Constants.ShaderRegister);

                for(int constantMapInd = (int) resConfigIndex; constantMapInd >= 0; constantMapInd--) {
                    const PipelineResourceMap<RootConstantInfo>& constantMap = constantMapArr[constantMapInd];
                    if(!constantMap.contains(shaderReg)) {
                        continue;
                    }
                    
                    const RootConstantInfo& constantInfo = constantMap.at(shaderReg);

                    WINRT_ASSERT(rootParam.Constants.Num32BitValues == constantInfo.num32BitValues);
                    
                    const auto newParam = std::make_shared<RootConstantsParameter>(i,
                                                                                   isCompute,
                                                                                   constantInfo.data,
                                                                                   constantInfo.num32BitValues
                                                                                   );

                    found = true;
                    outRootParameters.push_back(std::move(newParam));
                    break; 
                }
                
                WINRT_ASSERT(found);
            }
            else {
                bool found = false;
                
                for(int resMapInd = (int) resConfigIndex; resMapInd >= 0; resMapInd--) {
                    const PipelineResourceMap<ResourceInfo>& resMap = resMapArr[resConfigIndex];
                    
                    WINRT_ASSERT(paramToResourceType.contains(rootParam.ParameterType));

                    const ResourceDescriptorType resType = paramToResourceType.at(rootParam.ParameterType);
                    // find resource
                    const ShaderRegister shaderReg(resType,
                                             rootParam.Descriptor.RegisterSpace,
                                             rootParam.Descriptor.ShaderRegister);

                    if(!resMap.contains(shaderReg)) {
                        continue;
                    }
                    
                    WINRT_ASSERT(resMap.contains(shaderReg));

                    const ResourceInfo& resInfo = resMap.at(shaderReg);
                    WINRT_ASSERT(!resInfo.res.expired());

                    const auto newParam = std::make_shared<RootDescriptorParameter>(i,
                                                                                    isCompute,
                                                                                    resInfo.res.lock()->GetNativeResource().get(),
                                                                                    resType);
                    found = true;
                    outRootParameters.push_back(std::move(newParam));
                    break;
                }

                WINRT_ASSERT(found);
            }
        }

        return true;
    }
    
    bool ComputeDescriptorRangesFromContinuousResources(const RootParameterUsageMap& usageMap,
                                                        std::vector<DescriptorRangeDescription>& outRanges) {

        std::vector<DescriptorRangeDescription> rangeEntries;
        for(const auto&[key, registerSet] : usageMap) {
            if(registerSet.size() == 0) {
                // this may happen if we filtered out a used register tuple
                continue;
            }
            
            std::vector<uint16_t> registerVec(registerSet.begin(), registerSet.end());
            std::sort(registerVec.begin(), registerVec.end());
            
            uint16_t tail = 0;
            uint16_t head = 0;
            for(int i = 1; i < registerVec.size(); i++) {
                const uint16_t& curRegister = registerVec[i];

                if(curRegister == registerVec[head] + 1) {
                    // continuous
                    head++;
                }
                else {
                    WINRT_ASSERT(head - tail >= 0 && "Head should always be equal or greater than tail.");
                    
                    DescriptorRangeDescription newEntry {
                        .descriptorType = std::get<0>(key),
                        .registerSpace = std::get<1>(key),
                        .baseRegister = registerVec[tail],
                        .numDescriptors = uint16_t(head - tail + 1),
                    };

                    outRanges.push_back(newEntry);

                    tail = i;
                    head = i;
                }
            }

            // add the last one (since we either left the loop still continuing the streak,
            // or we just started a new one)
            DescriptorRangeDescription newEntry {
                .descriptorType = std::get<0>(key),
                .registerSpace = std::get<1>(key),
                .baseRegister = registerVec[tail],
                .numDescriptors = uint16_t(head - tail + 1),
            };

            outRanges.push_back(newEntry);
        }

        return true;
    }
    
    void InitializeDescriptorRanges(const std::vector<DescriptorRangeDescription>& ranges,
                                    const std::set<D3D12_DESCRIPTOR_RANGE_TYPE>& allowList,
                                    std::vector<D3D12_DESCRIPTOR_RANGE1>& outRanges) {
        static const std::map<ResourceDescriptorType, D3D12_DESCRIPTOR_RANGE_TYPE> typeMap = {
            {ResourceDescriptorType::SRV, D3D12_DESCRIPTOR_RANGE_TYPE_SRV},
            {ResourceDescriptorType::CBV, D3D12_DESCRIPTOR_RANGE_TYPE_CBV},
            {ResourceDescriptorType::UAV, D3D12_DESCRIPTOR_RANGE_TYPE_UAV},
            {ResourceDescriptorType::Sampler, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER},
        };

        for(const auto& range : ranges) {
            WINRT_ASSERT(typeMap.contains(range.descriptorType));
            const D3D12_DESCRIPTOR_RANGE_TYPE& rangeType = typeMap.at(range.descriptorType);
            if(!allowList.contains(rangeType)) {
                continue;
            }

            CD3DX12_DESCRIPTOR_RANGE1 newRange;
            newRange.Init(
                rangeType,
                range.numDescriptors,
                 range.baseRegister,
                 range.registerSpace,
                 D3D12_DESCRIPTOR_RANGE_FLAG_NONE, // TODO
                 D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND // TODO for bindless?
            );

            outRanges.push_back(newRange);
        }
    }
    
    void GenerateRootSignature(winrt::com_ptr<ID3D12Device> device,
                                                              const std::vector<std::weak_ptr<Shader>>& shaders,
                                                              const PipelineResourceMap<ResourceInfo>& resMap,
                                                              const PipelineResourceMap<RootConstantInfo>& constantMap,
                                                              const PipelineResourceMap<D3D12_SAMPLER_DESC>& samplerMap,
                                                              const PipelineResourceMap<D3D12_SAMPLER_DESC>& staticSamplerMap,
                                                              winrt::com_ptr<ID3DBlob>& outRSBlob,
                                                              winrt::com_ptr<ID3D12RootSignature>& outRS) {
        // given the resource usage, create a default root parameter configuration
        // flags can be used to explicitly describe how a resource should be accessed (root parameter, tables, etc.)

        // merge root parameter keys for all shaders in pipeline
        // std::vector<std::weak_ptr<Shader>> shaders;
        // pso->GetShaders(shaders);
        RootParameterUsageMap usageMap = ::GetMergedRootParameterUsageMap(shaders);
        std::vector<ShaderRegister> rootDescriptorDeclarations;

        // filter the resources that are marked to be non-descriptor table
        // (i.e. 32-bit constants and root-descriptor)
        for(auto& [typeAndSpace,regNums] : usageMap) {
            const ResourceDescriptorType& resType = std::get<0>(typeAndSpace);
            const uint16_t& regSpace = std::get<1>(typeAndSpace);

            auto removeNonTable = [&](uint16_t regNum)->bool {
                const ShaderRegister shaderReg(resType, regSpace, regNum);

                if(resMap.contains(shaderReg)) {
                    if(resMap.at(shaderReg).bindMethod == ResourceBindMethod::RootDescriptor) {
                        // remove from regNums, and add this shader register to root descriptors
                        rootDescriptorDeclarations.push_back(shaderReg);
                        std::cout << "Removing Root Descriptor: " << (int) resType << " " << regSpace << " " << regNum << std::endl;
                        return true; // remove
                    }
                }
                if(staticSamplerMap.contains(shaderReg)) {
                    std::cout << "Removing Static Sampler: " << (int) resType << " " << regSpace << " " << regNum << std::endl;
                    return true; // remove if this sampler is defined statically
                }
                if(constantMap.contains(shaderReg)) {
                    std::cout << "Removing 32Bit Constant: " << (int) resType << " " << regSpace << " " << regNum << std::endl;
                    return true;
                }
                return false; // keep
            };

            std::cout << "Size before: " << regNums.size() << std::endl;
            std::erase_if(regNums, removeNonTable);
            std::cout << "Size after: " << regNums.size() << std::endl;

        }
        
        // then gather the continuous ones (w/ same type)
        // bias towards create descriptor tables, unless specified otherwise (using flags)

        std::vector<DescriptorRangeDescription> ranges;
        ::ComputeDescriptorRangesFromContinuousResources(usageMap, ranges);

        // Create one descriptor table for all resource ranges,
        // Create another table for all sampler ranges
        std::vector<D3D12_ROOT_PARAMETER1> rootParams;

        std::vector<D3D12_DESCRIPTOR_RANGE1> resRanges;
        std::vector<D3D12_DESCRIPTOR_RANGE1> samplerRanges;

        ::InitializeDescriptorRanges(ranges, resourceRangeTypes, resRanges);
        ::InitializeDescriptorRanges(ranges, samplerRangeTypes, samplerRanges);

        uint32_t rootParamIndex = 0;

        if(resRanges.size() > 0) {
            CD3DX12_ROOT_PARAMETER1 param;
            CD3DX12_ROOT_PARAMETER1::InitAsDescriptorTable(param, resRanges.size(), resRanges.data());
            rootParams.push_back(param);
        }
        if(samplerRanges.size() > 0) {
            CD3DX12_ROOT_PARAMETER1 param;
            CD3DX12_ROOT_PARAMETER1::InitAsDescriptorTable(param, samplerRanges.size(), samplerRanges.data());
            rootParams.push_back(param);
        }

        // Create non-table root parameters
        for(const auto& rootDescShaderReg : rootDescriptorDeclarations) {
            CD3DX12_ROOT_PARAMETER1 param;
            
            // TODO: match exactly where this resource is needed
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
            D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
            
            switch(rootDescShaderReg.type) {
            case ResourceDescriptorType::SRV:
                param.InitAsShaderResourceView(rootDescShaderReg.regNumber, rootDescShaderReg.regSpace, flags, visibility);
                break;
            case ResourceDescriptorType::CBV:
                param.InitAsConstantBufferView(rootDescShaderReg.regNumber, rootDescShaderReg.regSpace, flags, visibility);
                break;
            case ResourceDescriptorType::UAV:
                param.InitAsUnorderedAccessView(rootDescShaderReg.regNumber, rootDescShaderReg.regSpace, flags, visibility);
                break;
            default:
                break;
            }

            rootParams.push_back(param);
        }

        // root constants
        for(const auto& [shaderReg, constantInfo]: constantMap) {
            CD3DX12_ROOT_PARAMETER1 param;
            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL;
            param.InitAsConstants(constantInfo.num32BitValues, shaderReg.regNumber, shaderReg.regSpace, visibility);

            rootParams.push_back(param);
        }

        // Create static samplers
        std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplerDescs;
        for(const auto& [shaderReg, sampler] : staticSamplerMap) {
            D3D12_SHADER_VISIBILITY visiblity = D3D12_SHADER_VISIBILITY_ALL;
            
            D3D12_STATIC_SAMPLER_DESC desc {
                .Filter = sampler.Filter,
                .AddressU = sampler.AddressU,
                .AddressV = sampler.AddressV,
                .AddressW = sampler.AddressW,
                .MipLODBias = sampler.MipLODBias,
                .MaxAnisotropy = sampler.MaxAnisotropy,
                .ComparisonFunc = sampler.ComparisonFunc,
                .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, // how to configure this?
                .MinLOD = sampler.MinLOD,
                .MaxLOD = sampler.MaxLOD,
                .ShaderRegister = shaderReg.regNumber,
                .RegisterSpace = shaderReg.regSpace,
                .ShaderVisibility = visiblity,
            };

            staticSamplerDescs.push_back(desc);
        }

        // Construct Root signature
        winrt::com_ptr<ID3DBlob> rsBlob;
        winrt::com_ptr<ID3DBlob> errorBlob;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC versionDesc;
        versionDesc.Init_1_1(
            rootParams.size(),
            rootParams.data(),
            staticSamplerDescs.size(),
            staticSamplerDescs.data(),
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        HRESULT hr = D3DX12SerializeVersionedRootSignature(&versionDesc,
                                                   D3D_ROOT_SIGNATURE_VERSION_1_1,
                                                   rsBlob.put(),
                                                   errorBlob.put()
                                                  );
        WINRT_ASSERT(SUCCEEDED(hr));

        winrt::com_ptr<ID3D12RootSignature> rootSig;
        hr = device->CreateRootSignature(0,
                                          rsBlob->GetBufferPointer(),
                                          rsBlob->GetBufferSize(),
                                          __uuidof(ID3D12RootSignature),
                                          rootSig.put_void());

        WINRT_ASSERT(SUCCEEDED(hr));
        
        // Continue to root signature processing...
        outRS = rootSig;
        outRSBlob = rsBlob;
    }
    
}
