#ifndef RENDERER_MEMORY_MEMORY_ALLOCATOR_H_
#define RENDERER_MEMORY_MEMORY_ALLOCATOR_H_

#include <functional>
#include <map>
#include <string>
#include <memory>
#include "resources.h"

class MemoryAllocator {
public:

    bool DoesResourceExist(std::string id) const;
    
    template<IsResource T, class ... _Types>
    requires std::is_constructible_v<T, _Types...>
    std::weak_ptr<T> CreateResource(std::string id, _Types&&... args);

    // TODO: maybe not needed, replace onCommitCallback with a callback directly tied to the resource (when it's fully done being made)
    void Commit() {
        CommitImplementation();
        
        for(auto callback : onCommitCallbacks) {
            callback();
        }
    };

    virtual void OnResourceCreated(std::shared_ptr<Resource> newResource) = 0;


    virtual void Update(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList,
                        winrt::com_ptr<ID3D12CommandQueue> cmdQueue) = 0;

    virtual bool HasWork() = 0;
    
    virtual ~MemoryAllocator() = default;

    void AddCommitCallback(std::function<void()> func) {
        onCommitCallbacks.push_back(func);
    }
    
protected:
    virtual void CommitImplementation() = 0;
    
    std::map<std::string, std::shared_ptr<Resource>> resourceMap_;
    
    std::vector<std::function<void()>> onCommitCallbacks;
};

template<IsResource T, class ... _Types>
requires std::is_constructible_v<T, _Types...>
std::weak_ptr<T> MemoryAllocator::CreateResource(std::string id, _Types&&... args) {
    WINRT_ASSERT(!resourceMap_.contains(id));
    
    if(resourceMap_.contains(id)) {
        return std::weak_ptr<T>();
    }
    
    std::shared_ptr<T> newRes = std::make_shared<T>(std::forward<_Types>(args)...);
    resourceMap_.insert({id, newRes});

    OnResourceCreated(newRes);


    return newRes;
}

#endif // RENDERER_MEMORY_MEMORY_ALLOCATOR_H_
