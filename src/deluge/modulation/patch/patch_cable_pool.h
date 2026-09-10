#pragma once

#include <cstddef>

class PatchCable;

// One firmware-wide cache of raw PatchCable storage. Active objects grow on demand;
// only unused storage is bounded. Allocation failure is reported with nullptr.
class patch_cable_pool {
public:
	static patch_cable_pool& get();
	PatchCable* acquire();
	void release(PatchCable* cable);
	void clear_unused();
	size_t active_count() const { return active_count_; }
	size_t cached_count() const { return cached_count_; }

private:
	patch_cable_pool() = default;
	patch_cable_pool(const patch_cable_pool&) = delete;
	patch_cable_pool& operator=(const patch_cable_pool&) = delete;
	struct free_slot {
		free_slot* next;
	};
	static constexpr size_t cache_limit = 32;
	free_slot* free_list_ = nullptr;
	size_t active_count_ = 0;
	size_t cached_count_ = 0;
};
