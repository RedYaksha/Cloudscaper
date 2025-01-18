#ifndef RENDERER_MEMORY_MEMORY_ALLOCATOR_H_
#define RENDERER_MEMORY_MEMORY_ALLOCATOR_H_

#include <map>
#include <string>
#include <memory>
#include "resources.h"

class MemoryAllocator {
public:

    bool DoesResourceExist(std::string id) const;
    
    template<IsResource T>
    std::shared_ptr<T> CreateResource(std::string id, typename T::Config p);

    virtual void Commit() = 0;

    virtual void Update(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList,
                        winrt::com_ptr<ID3D12CommandQueue> cmdQueue) = 0;

    virtual bool HasWork() = 0;
    
    virtual ~MemoryAllocator() = default;
protected:
    std::map<std::string, std::shared_ptr<Resource>> resource_map_; 
};

template <IsResource T>
std::shared_ptr<T> MemoryAllocator::CreateResource(std::string id, typename T::Config p) {
    if(resource_map_.contains(id)) {
        return nullptr;
    }
    
    std::shared_ptr<T> newRes = std::make_shared<T>(p);
    resource_map_.insert({id, newRes});

    return newRes;
}

#endif // RENDERER_MEMORY_MEMORY_ALLOCATOR_H_
