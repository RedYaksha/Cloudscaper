#ifndef RENDERER_MEMORY_STATIC_DESCRIPTOR_ALLOCATOR_H_
#define RENDERER_MEMORY_STATIC_DESCRIPTOR_ALLOCATOR_H_

#include "descriptor_allocator.h"

class StaticDescriptorAllocator : public DescriptorAllocator {
public:
    StaticDescriptorAllocator(winrt::com_ptr<ID3D12Device> device,
                                D3D12_DESCRIPTOR_HEAP_TYPE type,
                                uint32_t numDescriptors,
                                bool shaderVisible);
                                
    std::weak_ptr<DescriptorHeapAllocation> Allocate(uint32_t numDescriptors) override;
private:
    std::vector<std::shared_ptr<DescriptorHeapAllocation>> allocations_;

    std::mutex allocMutex_;
    uint32_t size_;
    uint32_t curIndex_;
    uint32_t incrementSize_;
};

#endif // RENDERER_MEMORY_STATIC_DESCRIPTOR_ALLOCATOR_H_
