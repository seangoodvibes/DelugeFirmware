#pragma once
#include "memory/general_memory_allocator.h"
#include "util/exceptions.h"
#include <cstddef>
extern "C" {
void abort(void); // this is defined in reset_handler.S
}
namespace deluge::memory {

/**
 * @brief A simple GMA wrapper that conforms to the C++ Allocator trait spec
 * (see: https://en.cppreference.com/w/cpp/named_req/Allocator)
 *
 * @tparam T The type to allocate for
 */
template <typename T, AllocationTag Tag>
class fast_allocator {
public:
	using value_type = T;

	template <typename U>
	struct rebind {
		using other = fast_allocator<U, Tag>;
	};

	constexpr fast_allocator() noexcept = default;

	template <typename U, AllocationTag OtherTag>
	constexpr fast_allocator(const fast_allocator<U, OtherTag>&) noexcept {};

	[[nodiscard]] T* allocate(std::size_t n) noexcept(false) {
		if (n == 0) {
			return nullptr;
		}
		void* addr = GeneralMemoryAllocator::get().allocMaxSpeedTagged(n * sizeof(T), Tag);
		if (addr == nullptr) [[unlikely]] {
			throw deluge::exception::BAD_ALLOC;
		}
		return static_cast<T*>(addr);
	}

	[[nodiscard]] T* allocate(std::size_t n, AllocationTag allocationTag) noexcept(false) {
		return allocateTagged(n, allocationTag);
	}

	[[nodiscard]] T* allocateTagged(std::size_t n, AllocationTag allocationTag) noexcept(false) {
		if (n == 0) {
			return nullptr;
		}
		void* addr = GeneralMemoryAllocator::get().allocMaxSpeedTagged(n * sizeof(T), allocationTag);
		if (addr == nullptr) [[unlikely]] {
			throw deluge::exception::BAD_ALLOC;
		}
		return static_cast<T*>(addr);
	}

	void deallocate(T* p, std::size_t n) { GeneralMemoryAllocator::get().dealloc(p); }

	template <typename U, AllocationTag OtherTag>
	bool operator==(const fast_allocator<U, OtherTag>& o) {
		return true;
	}

	template <typename U, AllocationTag OtherTag>
	bool operator!=(const fast_allocator<U, OtherTag>& o) {
		return false;
	}
};
} // namespace deluge::memory
