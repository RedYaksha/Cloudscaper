#include "directx/d3dx12.h"
#include "static_memory_allocator.h"

#include <iostream>

StaticMemoryAllocator::StaticMemoryAllocator(winrt::com_ptr<ID3D12Device> device)
    : device_(device), heapSize_(0) {

}

void StaticMemoryAllocator::CommitImplementation() {
    // go through all resources and see how big our D3D12_HEAP_TYPE_DEFAULT should be
    std::vector<D3D12_RESOURCE_DESC> resourceDescs;
    
    for(auto [id, res] : resourceMap_) {
        if(res->IsDynamic()) {
            continue;
        }
        
        D3D12_RESOURCE_DESC resDesc = res->CreateResourceDesc();
        resourceDescs.push_back(resDesc);
    }

    D3D12_RESOURCE_ALLOCATION_INFO allocInfo = device_->GetResourceAllocationInfo(0, resourceDescs.size(), resourceDescs.data());

    const uint64_t heapSize = allocInfo.SizeInBytes;
    const uint64_t globalAlignment = allocInfo.Alignment;

    {
        CD3DX12_HEAP_DESC desc = CD3DX12_HEAP_DESC(heapSize,
                                                    D3D12_HEAP_TYPE_DEFAULT,
                                                    globalAlignment, // if we have MSAA textures in this heap, we must use the other one (4MB)
                                                    D3D12_HEAP_FLAG_NONE);
        
        device_->CreateHeap(&desc, __uuidof(ID3D12Heap), heap_.put_void());
    }

    // The D3D12_HEAP_TYPE_UPLOAD will be the same size, but once
    // everything is uploaded, we will free this memory
    {
        CD3DX12_HEAP_DESC desc = CD3DX12_HEAP_DESC(heapSize,
                                                    D3D12_HEAP_TYPE_UPLOAD,
                                                    globalAlignment, // if we have MSAA textures in this heap, we must use the other one (4MB)
                                                    D3D12_HEAP_FLAG_NONE);
                                                    
        device_->CreateHeap(&desc, __uuidof(ID3D12Heap), uploadHeap_.put_void());
    }

    uint64_t defaultOffset = 0;
    uint64_t uploadOffset = 0;

    for(auto [id, res] : resourceMap_) {
        if(res->IsDynamic()) {
            continue;
        }
        
        D3D12_RESOURCE_DESC resDesc = res->CreateResourceDesc();

        D3D12_CLEAR_VALUE clearVal;
        D3D12_CLEAR_VALUE* ptrClearVal = NULL;
        if(res->GetOptimizedClearValue(clearVal)) {
            ptrClearVal = &clearVal;
        }
        
        // update heap offset
        D3D12_RESOURCE_ALLOCATION_INFO resAllocInfo = device_->GetResourceAllocationInfo(0, 1, &resDesc);
        
        assert(defaultOffset % resAllocInfo.Alignment == 0);
        assert(defaultOffset + resAllocInfo.SizeInBytes <= heapSize);

        winrt::com_ptr<ID3D12Resource> newRes;
        device_->CreatePlacedResource(heap_.get(),
                                      defaultOffset,
                                      &resDesc,
                                      res->GetResourceState(),
                                      ptrClearVal,
                                      __uuidof(ID3D12Resource),
                                      newRes.put_void());
        
        res->SetNativeResource(newRes);

        // https://stackoverflow.com/questions/2403631/how-do-i-find-the-next-multiple-of-10-of-any-integer
        // (n+9) - ((n+9)%10) - Mark Dickinson
        defaultOffset += resAllocInfo.SizeInBytes;
        
        const uint64_t A = allocInfo.Alignment;
        defaultOffset = (defaultOffset + (A - 1)) - ((defaultOffset + (A - 1)) % A);

        if(res->IsUploadNeeded()) {
            CD3DX12_RESOURCE_DESC uploadResDesc = CD3DX12_RESOURCE_DESC::Buffer(resAllocInfo);
            
            winrt::com_ptr<ID3D12Resource> uploadRes;
            device_->CreatePlacedResource(uploadHeap_.get(),
                                      uploadOffset,
                                      &uploadResDesc,
                                      res->GetResourceState(),
                                      ptrClearVal,
                                      __uuidof(ID3D12Resource),
                                      uploadRes.put_void());

            res->SetUploadResource(uploadRes);

            uploadOffset += resAllocInfo.SizeInBytes;
            uploadOffset = (uploadOffset + (A - 1)) - ((uploadOffset + (A - 1)) % A);

            uploadQueue_.push(res);
        }
        else {
            // no upload needed, can be use right away
            res->SetIsReady(true);
        }
    }
}

void StaticMemoryAllocator::OnResourceCreated(std::shared_ptr<Resource> newResource) {
    //
    if(newResource->IsDynamic()) {
        // we give the resource the function to call, so if it wants to reinitialize its data, then it can
        newResource->initializeDynamicResourceFunc_ = std::bind(&StaticMemoryAllocator::InitializeDynamicResource, this, newResource);
        InitializeDynamicResource(newResource);
        
        newResource->HandleDynamicUpload();
    }
}

void StaticMemoryAllocator::InitializeDynamicResource(std::shared_ptr<Resource> res) {
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC resDesc = res->CreateResourceDesc();

    HRESULT hr = device_->CreateCommittedResource(&heapProps,
                                                 D3D12_HEAP_FLAG_NONE,
                                                 &resDesc,
                                                 D3D12_RESOURCE_STATE_COMMON,
                                                 nullptr,
                                                 __uuidof(ID3D12Resource),
                                                 res->res_.put_void()
                                                 );
    WINRT_ASSERT(SUCCEEDED(hr));

    D3D12_RANGE readRange = {0, 0};
    res->res_->Map(0, &readRange, &res->dynamicResMappedPtr_);
}

void StaticMemoryAllocator::Update(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, winrt::com_ptr<ID3D12CommandQueue> cmdQueue) {
    
    //
    std::vector<std::shared_ptr<Resource>> resUploading;
    
    const uint32_t maxWorkNum = uploadQueue_.size();
    for(int i = 0; i < maxWorkNum; i++) {
        if(uploadQueue_.empty()) {
            break;
        }
        
        // - create in upload heap
        // - Map 
        // - memcpy (ideally in another thread and just check if done per Update())
        //          - for larger uploads (but playing with maxWorkNum can also affect performance)
        // - let Resource handle copying, and give the ptr in the UPLOAD_HEAP
        // -
        std::shared_ptr<Resource> res = uploadQueue_.front();
        res->HandleUpload(cmdList);
        
        uploadQueue_.pop();
        resUploading.push_back(res);
    }
    
    cmdList->Close();
    ID3D12CommandList* const cmdLists[] = {
        cmdList.get()
    };
    cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    // TODO: profile cost of creating one-off fences vs maintaining a pool of fences...
    winrt::com_ptr<ID3D12Fence> waitFence;
    device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), waitFence.put_void());
    cmdQueue->Signal(waitFence.get(), 1);

    // TODO: thread pool
    std::thread t(&StaticMemoryAllocator::WaitForUploadComplete, this, waitFence, resUploading);
    t.detach();
}

bool StaticMemoryAllocator::HasWork() {
    return uploadQueue_.size() > 0;
}

void StaticMemoryAllocator::WaitForUploadComplete(winrt::com_ptr<ID3D12Fence> fence, std::vector<std::shared_ptr<Resource>> resUploading) {
	HANDLE fe = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fe);

    std::cout << "starting upload fence" << std::endl;
	
	if(fence->GetCompletedValue() < 1) {
		fence->SetEventOnCompletion(1, fe);
		WaitForSingleObject(fe, INFINITE); // block
	}
	
	std::cout << "upload fence complete!" << std::endl;

    for(std::shared_ptr<Resource>& res : resUploading) {
        res->SetIsReady(true);
    }
}

StaticMemoryAllocator::~StaticMemoryAllocator() {
    std::cout << "Destroying memory allocator..." << std::endl;
    
}
