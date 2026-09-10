#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

// Shift a logical suffix within existing circular storage. No allocation,
// callbacks, construction or destruction occurs; the caller initializes the gap.
inline bool insert_into_reserved_ring(void* memory, int32_t capacity, int32_t start, int32_t& count,
                                      uint32_t elementSize, int32_t index, int32_t amount) {
	if (!memory || capacity <= 0 || start < 0 || start >= capacity || count < 0 || count > capacity || index < 0
	    || index > count || amount <= 0 || amount > capacity - count || elementSize == 0
	    || static_cast<size_t>(capacity) > std::numeric_limits<size_t>::max() / elementSize) {
		return false;
	}
	auto address = [&](int32_t logical) {
		return static_cast<unsigned char*>(memory)
		       + ((static_cast<size_t>(start) + logical) % static_cast<size_t>(capacity)) * elementSize;
	};
	for (int32_t source = count; source > index; --source) {
		std::memmove(address(source - 1 + amount), address(source - 1), elementSize);
	}
	count += amount;
	return true;
}
