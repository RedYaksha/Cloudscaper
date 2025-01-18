#include "memory_allocator.h"

bool MemoryAllocator::DoesResourceExist(std::string id) const {
    return resource_map_.contains(id);
}
