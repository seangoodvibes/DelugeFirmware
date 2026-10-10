#pragma once
#include <cstdint>
#include <limits>

namespace deluge::model {
// Cooperative model construction only. Never reuse an identity within a boot;
// zero denotes an unavailable identity if the counter is exhausted.
inline uint64_t next_note_row_identity() {
	static uint64_t next = 0;
	if (next == std::numeric_limits<uint64_t>::max())
		return 0;
	return ++next;
}
} // namespace deluge::model
