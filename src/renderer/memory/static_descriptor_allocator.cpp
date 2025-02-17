#include "directx/d3dx12.h"
#include "static_descriptor_allocator.h"

StaticDescriptorAllocator::StaticDescriptorAllocator(
                                        winrt::com_ptr<ID3D12Device> device,
                                        D3D12_DESCRIPTOR_HEAP_TYPE type,
                                        uint32_t numDescriptors,
                                        bool shaderVisible)
    : DescriptorAllocator(device, type, shaderVisible), curIndex_(0) {


    D3D12_DESCRIPTOR_HEAP_DESC desc;
    desc.Type = type_;
    desc.Flags = isShaderVisible_? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NumDescriptors = numDescriptors;
    desc.NodeMask = 0;
    
    device_->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), descriptorHeap_.put_void());
    incrementSize_ = device_->GetDescriptorHandleIncrementSize(type_);
    size_ = numDescriptors;
}

std::weak_ptr<DescriptorHeapAllocation> StaticDescriptorAllocator::Allocate(uint32_t numDescriptors) {
    // NOTE: we could probably just lock our use of allocations_ and curIndex_,
    // and modify a reference to the new DescriptorHeapAllocation, but
    // if our allocations_ is not a fixed size, the underlying memory
    // could change while we're modifying the allocation pointer...
    // So we just lock this whole function (which is not complex, anyway)
    std::lock_guard<std::mutex> lock(allocMutex_);
    
    // check if we have enough space for the allocation
    if(curIndex_ + numDescriptors > size_) {
        return std::weak_ptr<DescriptorHeapAllocation>();
    }
    
    const uint32_t allocationOffset = curIndex_;
    curIndex_ += numDescriptors;

    std::shared_ptr<DescriptorHeapAllocation> allocation = std::make_shared<DescriptorHeapAllocation>();

    allocation->offset_ = allocationOffset;
    allocation->size_ = numDescriptors;
    allocation->incrementSize_ = incrementSize_;
    
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    cpuHandle.InitOffsetted(GetCPUHeapBase(), allocationOffset, incrementSize_);
    allocation->cpuHandle_ = cpuHandle;

    if(isShaderVisible_) {
        CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle;
        gpuHandle.InitOffsetted(GetGPUHeapBase(), allocationOffset, incrementSize_);
        allocation->gpuHandle_ = gpuHandle;
    }

    allocations_.push_back(std::move(allocation));
    return allocations_.back();
}
