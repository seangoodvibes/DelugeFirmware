#include "memory/general_memory_allocator.h"
#include "util/exceptions.h"
#include <algorithm>
#include <limits>
#include <new>

// todo - make this work in unit tests, need to remove hard coded addresses in GMA
#if !IN_UNIT_TESTS
void* operator new(std::size_t n) noexcept(false) {
	// allocate on external RAM
	void* addr = GeneralMemoryAllocator::get().allocExternal(n);
	if (addr == nullptr) [[unlikely]] {
		// The throwing operator new must never return null: std::string / std::vector and friends assume a non-null
		// buffer, so returning null silently corrupts them. Throw the same lightweight exception the custom STL
		// allocators (sdram_allocator / external_allocator) do, so existing `catch (deluge::exception)` out-of-memory
		// handlers can deal with it. Code that wants graceful failure uses the GeneralMemoryAllocator directly.
		throw deluge::exception::BAD_ALLOC;
	}
	return addr;
}

void operator delete(void* p) {
	delugeDealloc(p);
}

// C++14 sized deallocation. libstdc++ containers (std::string, std::vector, ...) call the sized form directly. On the
// device the default sized delete defers to the unsized one above, but a sanitizer substitutes its own sized-delete
// that does NOT defer - so without this override, std::string's buffer (allocated via our operator new) is freed
// through the sanitizer and reported as a bad free. Route it to the custom allocator like the unsized form.
void operator delete(void* p, std::size_t) noexcept {
	delugeDealloc(p);
}
// The toolchain's aligned new uses newlib's memalign heap, which the firmware does not provide.
// Keep aligned allocations on the Deluge heap, just like the ordinary operators above.
void* operator new(std::size_t n, std::align_val_t alignment, const std::nothrow_t&) noexcept {
	const auto align = static_cast<std::size_t>(alignment);
	if (align == 0 || (align & (align - 1)) != 0) {
		return nullptr;
	}
	const auto mask = std::max(align, alignof(void*)) - 1;
	constexpr auto maxSize = std::numeric_limits<uint32_t>::max();
	if (mask > (maxSize - sizeof(void*)) / 2 || n > maxSize - 2 * mask - sizeof(void*)) {
		return nullptr;
	}
	// Round the payload up too, so cache-aligned buffers do not share their final cache line.
	auto* allocation = GeneralMemoryAllocator::get().allocLowSpeed(((n + mask) & ~mask) + mask + sizeof(void*));
	if (!allocation) {
		return nullptr;
	}
	auto aligned = (reinterpret_cast<uintptr_t>(allocation) + sizeof(void*) + mask) & ~mask;
	auto* buffer = reinterpret_cast<void**>(aligned);
	buffer[-1] = allocation;
	return buffer;
}

void* operator new(std::size_t n, std::align_val_t alignment) {
	void* address = ::operator new(n, alignment, std::nothrow);
	if (!address) {
		throw deluge::exception::BAD_ALLOC;
	}
	return address;
}

void operator delete(void* p, std::align_val_t) noexcept {
	if (p) {
		delugeDealloc(static_cast<void**>(p)[-1]);
	}
}

void operator delete(void* p, std::size_t, std::align_val_t alignment) noexcept {
	::operator delete(p, alignment);
}

void operator delete(void* p, std::align_val_t alignment, const std::nothrow_t&) noexcept {
	::operator delete(p, alignment);
}
#endif
