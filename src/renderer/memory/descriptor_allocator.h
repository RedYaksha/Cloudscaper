#ifndef RENDERER_MEMORY_DESCRIPTOR_ALLOCATOR_H_
#define RENDERER_MEMORY_DESCRIPTOR_ALLOCATOR_H_
#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include "renderer_types.h"
#include <directx/d3dx12_root_signature.h>

class DescriptorAllocator;
class StaticDescriptorAllocator;

class DescriptorHeapAllocation {
public:
    DescriptorHeapAllocation() = default;

    bool GetCPUDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE& outHandle) const;
    bool GetGPUDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE& outHandle) const;
    bool GetCPUDescriptorHandleOffsetted(uint32_t offset, D3D12_CPU_DESCRIPTOR_HANDLE& handle) const;
    bool GetGPUDescriptorHandleOffsetted(uint32_t offset, D3D12_GPU_DESCRIPTOR_HANDLE& handle) const;

    uint32_t GetAllocationSize() const { return size_; }

private:
    friend DescriptorAllocator;
    friend StaticDescriptorAllocator;
    
    
    uint32_t offset_; // offset from heap start, in descriptors
    uint32_t size_;
    uint32_t incrementSize_;

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle_;
    std::optional<CD3DX12_GPU_DESCRIPTOR_HANDLE> gpuHandle_; // parent heap may not be gpu visible
};

class DescriptorAllocator {
public:
    virtual ~DescriptorAllocator() = default;

    DescriptorAllocator(winrt::com_ptr<ID3D12Device> device, D3D12_DESCRIPTOR_HEAP_TYPE type, bool isShaderVisible)
        : device_(device), type_(type), isShaderVisible_(isShaderVisible)
        {}

    virtual std::weak_ptr<DescriptorHeapAllocation> Allocate(uint32_t numDescriptors) = 0;
    
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHeapBase() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHeapBase() const;
    winrt::com_ptr<ID3D12DescriptorHeap> GetDescriptorHeap() const { return descriptorHeap_; }

protected:
    winrt::com_ptr<ID3D12DescriptorHeap> descriptorHeap_;
    winrt::com_ptr<ID3D12Device> device_;
    D3D12_DESCRIPTOR_HEAP_TYPE type_;
    bool isShaderVisible_;
};

#endif // RENDERER_MEMORY_DESCRIPTOR_ALLOCATOR_H_