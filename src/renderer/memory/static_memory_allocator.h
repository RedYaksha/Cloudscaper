#ifndef RENDERER_MEMORY_STATIC_MEMORY_ALLOCATOR_H_
#define RENDERER_MEMORY_STATIC_MEMORY_ALLOCATOR_H_

#include <queue>

#include "memory_allocator.h"
#include "renderer_types.h"


struct UploadTask {
    std::string fileName;
    std::string texID;
private:

    //friend StaticDescriptorAllocator;
    void* srcImage;
};

//
// A (very) simple memory allocator that will
// store all resources in a default heap of fixed size
//
// For transferring data (e.g. images -> textures, uploading (static) vertex buffers),
// an upload queue is used, and one can refer to Resource::IsReady() for use. 
//
// Once StaticMemoryAllocator::Commit() is called, no further changes in the memory layout can be made.
// Great for applications where memory is static and the size is known before hand.
//
class StaticMemoryAllocator : public MemoryAllocator {
public:
    StaticMemoryAllocator(winrt::com_ptr<ID3D12Device> device);
    ~StaticMemoryAllocator();
    
    void Commit() override;
    void Update(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, winrt::com_ptr<ID3D12CommandQueue> cmdQueue);
    bool HasWork() override;
    
private:
    void WaitForUploadComplete(winrt::com_ptr<ID3D12Fence> fence, std::vector<std::shared_ptr<Resource>> resUploading);

    std::queue<std::shared_ptr<Resource>> uploadQueue_;
    uint32_t heapSize_;
    winrt::com_ptr<ID3D12Device> device_;
    winrt::com_ptr<ID3D12Heap> heap_;
    winrt::com_ptr<ID3D12Heap> uploadHeap_;
    
    // std::vector<std::thread> uploadFenceThreads_;
    // std::vector<std::atomic_bool> uploadFenceThreadDone_;
};

#endif // RENDERER_MEMORY_STATIC_MEMORY_ALLOCATOR_H_
