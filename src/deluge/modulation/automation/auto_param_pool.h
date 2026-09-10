#pragma once

#include <cstddef>

class AutoParam;

// One firmware-wide cache of raw AutoParam storage. Active objects grow on demand;
// only unused storage is bounded. Allocation failure is reported with nullptr.
class auto_param_pool {
public:
	static auto_param_pool& get();
	AutoParam* acquire();
	void release(AutoParam* param);
	void clear_unused();
	size_t active_count() const { return active_count_; }
	size_t cached_count() const { return cached_count_; }

private:
	auto_param_pool() = default;
	auto_param_pool(const auto_param_pool&) = delete;
	auto_param_pool& operator=(const auto_param_pool&) = delete;
	struct free_slot {
		free_slot* next;
	};
	static constexpr size_t cache_limit = 32;
	free_slot* free_list_ = nullptr;
	size_t active_count_ = 0;
	size_t cached_count_ = 0;
};
