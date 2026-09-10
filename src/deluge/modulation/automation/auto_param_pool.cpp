#include "modulation/automation/auto_param_pool.h"
#include "memory/general_memory_allocator.h"
#include "modulation/automation/auto_param.h"
#include <new>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define AUTO_PARAM_POOL_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define AUTO_PARAM_POOL_ASAN 1
#endif
#ifdef AUTO_PARAM_POOL_ASAN
#include <sanitizer/asan_interface.h>
#endif

auto_param_pool& auto_param_pool::get() {
	static auto_param_pool pool;
	return pool;
}

AutoParam* auto_param_pool::acquire() {
	static_assert(sizeof(AutoParam) >= sizeof(free_slot));
	static_assert(alignof(AutoParam) >= alignof(free_slot));
	void* memory;
	if (free_list_) {
		memory = free_list_;
		free_list_ = free_list_->next;
		--cached_count_;
	}
	else {
		memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(AutoParam));
		if (!memory)
			return nullptr;
	}
#ifdef AUTO_PARAM_POOL_ASAN
	__asan_unpoison_memory_region(memory, sizeof(AutoParam));
#endif
	++active_count_;
	return new (memory) AutoParam;
}

void auto_param_pool::release(AutoParam* param) {
	if (!param)
		return;
	param->~AutoParam();
	--active_count_;
	if (cached_count_ < cache_limit) {
		free_list_ = new (param) free_slot{free_list_};
		++cached_count_;
#ifdef AUTO_PARAM_POOL_ASAN
		__asan_poison_memory_region(reinterpret_cast<char*>(param) + sizeof(free_slot),
		                            sizeof(AutoParam) - sizeof(free_slot));
#endif
	}
	else {
		GeneralMemoryAllocator::get().dealloc(param);
	}
}

void auto_param_pool::clear_unused() {
	while (free_list_) {
		auto* slot = free_list_;
		free_list_ = slot->next;
#ifdef AUTO_PARAM_POOL_ASAN
		__asan_unpoison_memory_region(slot, sizeof(AutoParam));
#endif
		GeneralMemoryAllocator::get().dealloc(slot);
	}
	cached_count_ = 0;
}
