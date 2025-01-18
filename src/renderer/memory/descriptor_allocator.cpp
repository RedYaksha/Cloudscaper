#include "descriptor_allocator.h"


bool DescriptorHeapAllocation::GetCPUDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle) const {
    outHandle = cpuHandle_;
    return true;
}

bool DescriptorHeapAllocation::GetGPUDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE& outHandle) const {
    if(!gpuHandle_.has_value()) {
        return false;
    }
    outHandle = gpuHandle_.value();
    return true;
}

bool DescriptorHeapAllocation::GetCPUDescriptorHandleOffsetted(uint32_t offset,
    D3D12_CPU_DESCRIPTOR_HANDLE& handle) const {

    if(offset >= size_) {
        return false;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE out = cpuHandle_;
    out.Offset(static_cast<int>(offset), incrementSize_);
    
    handle = out;
    return true;
}

bool DescriptorHeapAllocation::GetGPUDescriptorHandleOffsetted(uint32_t offset,
    D3D12_GPU_DESCRIPTOR_HANDLE& handle) const {

    if(!gpuHandle_.has_value()) {
        return false;
    }
    
    if(offset >= size_) {
        return false;
    }

    CD3DX12_GPU_DESCRIPTOR_HANDLE out = gpuHandle_.value();
    out.Offset(static_cast<int>(offset), incrementSize_);
    
    handle = out;
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocator::GetCPUHeapBase() const {
    WINRT_ASSERT(descriptorHeap_);
    return descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocator::GetGPUHeapBase() const {
    WINRT_ASSERT(descriptorHeap_ && isShaderVisible_);
    return descriptorHeap_->GetGPUDescriptorHandleForHeapStart();
}
