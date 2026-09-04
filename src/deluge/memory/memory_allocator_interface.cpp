#include "general_memory_allocator.h"

void* allocMaxSpeed(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, true, false, thingNotToStealFrom, AllocationTag::GENERIC);
}

void* allocLowSpeed(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false, false, thingNotToStealFrom, AllocationTag::GENERIC);
}

void* allocStealable(uint32_t requiredSize, void* thingNotToStealFrom = nullptr) {
	return GeneralMemoryAllocator::get().alloc(requiredSize, false, true, thingNotToStealFrom, AllocationTag::GENERIC);
}
