#include "modulation/patch/patch_cable_pool.h"
#include "memory/general_memory_allocator.h"
#include "modulation/patch/patch_cable.h"
#include <new>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PATCH_CABLE_POOL_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define PATCH_CABLE_POOL_ASAN 1
#endif
#ifdef PATCH_CABLE_POOL_ASAN
#include <sanitizer/asan_interface.h>
#endif

patch_cable_pool& patch_cable_pool::get() {
	static patch_cable_pool pool;
	return pool;
}

PatchCable* patch_cable_pool::acquire() {
	static_assert(sizeof(PatchCable) >= sizeof(free_slot));
	static_assert(alignof(PatchCable) >= alignof(free_slot));
	void* memory;
	if (free_list_) {
		memory = free_list_;
		free_list_ = free_list_->next;
		--cached_count_;
	}
	else {
		memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(PatchCable));
		if (!memory)
			return nullptr;
	}
#ifdef PATCH_CABLE_POOL_ASAN
	__asan_unpoison_memory_region(memory, sizeof(PatchCable));
#endif
	++active_count_;
	return new (memory) PatchCable;
}

void patch_cable_pool::release(PatchCable* cable) {
	if (!cable)
		return;
	cable->~PatchCable();
	--active_count_;
	if (cached_count_ < cache_limit) {
		free_list_ = new (cable) free_slot{free_list_};
		++cached_count_;
#ifdef PATCH_CABLE_POOL_ASAN
		__asan_poison_memory_region(reinterpret_cast<char*>(cable) + sizeof(free_slot),
		                            sizeof(PatchCable) - sizeof(free_slot));
#endif
	}
	else {
		GeneralMemoryAllocator::get().dealloc(cable);
	}
}

void patch_cable_pool::clear_unused() {
	while (free_list_) {
		auto* slot = free_list_;
		free_list_ = slot->next;
#ifdef PATCH_CABLE_POOL_ASAN
		__asan_unpoison_memory_region(slot, sizeof(PatchCable));
#endif
		GeneralMemoryAllocator::get().dealloc(slot);
	}
	cached_count_ = 0;
}
