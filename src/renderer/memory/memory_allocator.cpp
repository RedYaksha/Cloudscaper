#include "memory_allocator.h"

bool MemoryAllocator::DoesResourceExist(std::string id) const {
    return resourceMap_.contains(id);
}
