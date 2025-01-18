#include "pipeline_assembler.h"

#include "resources.h"
#include "shader.h"
#include "memory/descriptor_allocator.h"


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

typedef std::map<ShaderRegister, DescriptorAllocationInfo> RegisterToDescriptorAllocationMap;

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
                                         // std::shared_ptr<PipelineState>& pso,
                                         const std::map<ShaderRegister, ResourceInfo>& resMap,
                                         const RegisterToDescriptorAllocationMap& allocations);
    
    bool InitializeDescriptorTableRootParameters(const std::vector<DescriptorTableDescription>& tables,
                                                 const bool& isCompute,
                                                 std::vector<std::shared_ptr<RootParameter>>& outRootParameters);

    bool InitializeNonDescriptorTableRootParametersFromRootSignature(const D3D12_ROOT_SIGNATURE_DESC* rootSigDesc,
                                                                     const bool& isCompute,
                                                                     const std::map<ShaderRegister, ResourceInfo>& resMap,
                                                                     const std::map<ShaderRegister, RootConstantInfo>& constantMap,
                                                                     std::vector<std::shared_ptr<RootParameter>>& outRootParameters);

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

void PipelineAssembler::Flush() {
    while(queue_.size() > 0) {
        std::weak_ptr<PipelineState> pso = std::move(queue_.front());
        queue_.pop();

        std::promise<PipelineState::State>& statePromise = pso.lock()->promise_;

        std::packaged_task<PipelineState::State()> task(std::bind(&PipelineAssembler::AssemblePipeline, this, pso, std::ref(statePromise)));
        // pso.lock()->future_ = task.get_future();
        
        threadPool_->AddTask(std::move(task));
    }
}

PipelineState::State PipelineAssembler::AssemblePipeline(std::weak_ptr<PipelineState> inPso, std::promise<PipelineState::State>& statePromise) {
    std::shared_ptr<PipelineState> pso = inPso.lock();

    std::vector<std::weak_ptr<Shader>> shaders;
    pso->GetShaders(shaders);

    // wait for every shader to finish compiling
    for(const auto& s : shaders) {
        std::shared_ptr<Shader> shader = s.lock();
        std::cout << std::format("Waiting for {}", shader->GetSourceFile()) << std::endl;

        const Shader::State& shaderState = shader->GetState_Block();
        
        if(shaderState.type != Shader::StateType::Ok) {
            PipelineState::State out;
            out.type = PipelineState::StateType::Ok;
            out.msg = std::format("Shader ({}) failed to compile. Pipeline assembly failed. {}", s.lock()->GetSourceFile(), pso->id_); 
            std::cout << out.msg << std::endl;
            
            statePromise.set_value(out);
            return out;
        }
        
        std::cout << "Success!" << shader->GetSourceFile() << std::endl;
    }

    std::cout << "Assembling pipeline..." << std::endl;
    if(pso->type_ == PipelineStateType::Graphics) {
        AssembleGraphicsPipeline(std::static_pointer_cast<GraphicsPipelineState>(pso), statePromise);
    }
    else {
        AssembleComputePipeline(std::static_pointer_cast<ComputePipelineState>(pso), statePromise);
    }

    //

    // Try root sig
    // std::weak_ptr<Shader::CompilationData> compileData = pso->
    

    PipelineState::State out;
    out.type = PipelineState::StateType::Ok;
    out.msg = "";
    
    statePromise.set_value(out);
    return out;
}

void PipelineAssembler::AssembleGraphicsPipeline(std::weak_ptr<GraphicsPipelineState> inPso,
                                                 std::promise<PipelineState::State>& statePromise) {
    
    WINRT_ASSERT(!resourceDescriptorAllocator_.expired());
    std::shared_ptr<DescriptorAllocator> resDescriptorAllocator = resourceDescriptorAllocator_.lock();
    std::shared_ptr<GraphicsPipelineState> pso = inPso.lock();
    std::shared_ptr<PipelineState> basePso = pso;

    HRESULT hr;
    

    // TODO: always assume vertex shader's root signature (if any)?
    
    std::shared_ptr<Shader::CompilationData> compileData = pso->vertexShader_.lock()->GetState_Block().compileData;

    if(compileData->rootSigBlob) {
        // HLSL defined RootSignatures - create proper descriptor data (tables, root params, etc.)
        const LPVOID rootSigPtr = compileData->rootSigBlob->GetBufferPointer();
        const SIZE_T rootSigSize = compileData->rootSigBlob->GetBufferSize();
        
        winrt::com_ptr<ID3D12RootSignature> rootSig;
        hr = device_->CreateRootSignature(0,
            rootSigPtr,
            rootSigSize,
            __uuidof(ID3D12RootSignature),
            rootSig.put_void());
        winrt::check_hresult(hr);
        
        winrt::com_ptr<ID3D12RootSignatureDeserializer> deserializer;
        hr = D3D12CreateRootSignatureDeserializer(rootSigPtr, rootSigSize, __uuidof(ID3D12RootSignatureDeserializer), deserializer.put_void());
        winrt::check_hresult(hr);

        const D3D12_ROOT_SIGNATURE_DESC* rootSigDesc = deserializer->GetRootSignatureDesc();

        // get all descriptor tables
        std::vector<DescriptorTableDescription> descriptorTables;
        ::ExtractAllRootSignatureDescriptorTablesWithType(rootSigDesc,
                                                          ::resourceRangeTypes,
                                                          descriptorTables);

        // allocate descriptors for all descriptor tables
        RegisterToDescriptorAllocationMap allocationMap;
        ::CreateDescriptorAllocationsFromTables(resDescriptorAllocator,
                                                descriptorTables,
                                                allocationMap);
        
        // init descriptors, according to linked resources
        bool success = ::InitializeDescriptorAllocations(device_, basePso->resMap_, allocationMap);
        WINRT_ASSERT(success);
        
        // create RootParameters so pipeline knows what to bind at render time
        std::vector<std::shared_ptr<RootParameter>> rootParams;
        const bool isCompute = basePso->type_ == PipelineStateType::Compute;
        ::InitializeDescriptorTableRootParameters(descriptorTables,
                                                  isCompute,
                                                  rootParams);
        
        ::InitializeNonDescriptorTableRootParametersFromRootSignature(rootSigDesc,
                                                                      isCompute,
                                                                      basePso->resMap_,
                                                                      basePso->constantMap_,
                                                                      rootParams);

        // create all the other RootParameters that are non-descriptor table
        

        std::cout << "root sig done" << std::endl;
    }
    else {
        // given the resource usage, create a default root parameter configuration
        // flags can be used to explicitly describe how a resource should be accessed (root parameter, tables, etc.)

        // merge root parameter keys for all shaders in pipeline
        std::vector<std::weak_ptr<Shader>> shaders;
        pso->GetShaders(shaders);
        RootParameterUsageMap rpMap = ::GetMergedRootParameterUsageMap(shaders);

        // filter the resources that are marked to be non-descriptor table
        // (i.e. 32-bit constants and root-descriptor)

        // then gather the continuous ones (w/ same type)
        // bias towards create descriptor tables, unless specified otherwise (using flags)
        struct DescriptorTableCreationEntry {
            ResourceDescriptorType descriptorType;
            uint16_t registerSpace;
            uint16_t baseRegister;
            uint16_t numDescriptors;
        };

        std::vector<DescriptorTableCreationEntry> creationEntries;
        for(const auto&[key, registerSet] : rpMap) {
            WINRT_ASSERT(registerSet.size() > 0 && "Register set empty, but key was added...");
            
            std::vector<uint16_t> registerVec(registerSet.begin(), registerSet.end());
            std::sort(registerVec.begin(), registerVec.end());
            
            uint16_t tail = 0;
            uint16_t head = 0;
            for(int i = 1; i < registerVec.size(); i++) {
                const uint16_t& curRegister = registerVec[i];

                if(curRegister == registerVec[head] + 1) {
                    // continous
                    head++;
                }
                else {
                    WINRT_ASSERT(head - tail >= 0 && "Head should always be equal or greater than tail.");
                    
                    DescriptorTableCreationEntry newEntry {
                        .descriptorType = std::get<0>(key),
                        .registerSpace = std::get<1>(key),
                        .baseRegister = registerVec[tail],
                        .numDescriptors = uint16_t(head - tail + 1),
                    };

                    creationEntries.push_back(newEntry);

                    tail = i;
                    head = i;
                }
            }

            // add the last one (since we either left the loop still continuing the streak,
            // or we just started a new one)
            DescriptorTableCreationEntry newEntry {
                .descriptorType = std::get<0>(key),
                .registerSpace = std::get<1>(key),
                .baseRegister = registerVec[tail],
                .numDescriptors = uint16_t(head - tail + 1),
            };

            creationEntries.push_back(newEntry);

            /////
            
            // TODO: create root signature, according to our entries, flags, etc.

            // TODO: allocate descriptors for all descriptor tables
            // may use: Resource or Sampler descriptor allocators

            // TODO: init descriptors, according to linked resources
            // 

            // TODO: 
            
            // TODO: create RootParameter so pipeline knows what to bind at render time

        }
        

        std::cout << creationEntries.size() + 1 << std::endl;
        //
        //
    }
}

void PipelineAssembler::AssembleComputePipeline(std::weak_ptr<ComputePipelineState> pso,
    std::promise<PipelineState::State>& statePromise) {
    
    return;
}

bool PipelineAssembler::Enqueue(std::weak_ptr<PipelineState> pso) {
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
        // should have flag to do this?
        outDescriptorTables.clear();

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

            outDescriptorTables.push_back(descriptorTable);
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
                                         const std::map<ShaderRegister, ResourceInfo>& resMap,
                                         const RegisterToDescriptorAllocationMap& allocations) {
        WINRT_ASSERT(device);
                                         
        for(const auto& [shaderReg, val] : allocations) {
            const std::shared_ptr<DescriptorHeapAllocation> allocation = val.allocation.lock();

            // find resource
            if(!resMap.contains(shaderReg)) {
                std::cout << std::format("Failed to find shader register! ({},{},{})",
                    (uint8_t) shaderReg.type, shaderReg.regSpace, shaderReg.regNumber) << std::endl;
                return false;
            }

            // find correct location in allocation
            const ResourceInfo& resInfo = resMap.at(shaderReg);
            WINRT_ASSERT(resInfo.bindMethod == ResourceBindMethod::Automatic ||
                         resInfo.bindMethod == ResourceBindMethod::DescriptorTable);

            WINRT_ASSERT(!resInfo.res.expired());
            std::shared_ptr<const Resource> res = resInfo.res.lock();

            const uint32_t offset = val.offsetFromAllocBase;

            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
            const bool success = allocation->GetCPUDescriptorHandleOffsetted(offset, cpuHandle);
            WINRT_ASSERT(success);

            const HRESULT hr = res->CreateDescriptor(cpuHandle);
            if(FAILED(hr)) {
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
                                                                     const std::map<ShaderRegister, ResourceInfo>& resMap,
                                                                     const std::map<ShaderRegister, RootConstantInfo>& constantMap,
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
                const ShaderRegister shaderReg(ResourceDescriptorType::Unknown,
                                         rootParam.Constants.RegisterSpace,
                                         rootParam.Constants.ShaderRegister);


                WINRT_ASSERT(constantMap.contains(shaderReg));
                const RootConstantInfo& constantInfo = constantMap.at(shaderReg);

                WINRT_ASSERT(rootParam.Constants.Num32BitValues == constantInfo.num32BitValues);
                
                const auto newParam = std::make_shared<RootConstantsParameter>(i,
                                                                               isCompute,
                                                                               constantInfo.data,
                                                                               constantInfo.num32BitValues
                                                                               );
                
                outRootParameters.push_back(std::move(newParam));
            }
            else {
                WINRT_ASSERT(paramToResourceType.contains(rootParam.ParameterType));

                const ResourceDescriptorType resType = paramToResourceType.at(rootParam.ParameterType);
                // find resource
                const ShaderRegister shaderReg(resType,
                                         rootParam.Descriptor.RegisterSpace,
                                         rootParam.Descriptor.ShaderRegister);
                WINRT_ASSERT(resMap.contains(shaderReg));

                const ResourceInfo& resInfo = resMap.at(shaderReg);
                WINRT_ASSERT(!resInfo.res.expired());

                const auto newParam = std::make_shared<RootDescriptorParameter>(i,
                                                                                isCompute,
                                                                                resInfo.res.lock()->GetNativeResource().get(),
                                                                                resType);
                outRootParameters.push_back(std::move(newParam));
            }
        }

        return true;
    }
    
}
