#pragma once
#include <cstdint>
#include <limits>

namespace deluge::model {
// Cooperative action construction. Zero signals identity exhaustion; never wrap.
inline uint64_t next_action_identity() {
	static uint64_t next = 0;
	if (next == std::numeric_limits<uint64_t>::max())
		return 0;
	return ++next;
}
} // namespace deluge::model
