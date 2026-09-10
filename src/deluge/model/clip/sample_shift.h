#pragma once
#include <cstdint>
#include <limits>
#include <optional>

namespace deluge::model {

// Preserve truncation toward zero without signed multiplication or subtraction
// overflow. A sample end beyond the file is allowed, as in the existing editor.
constexpr std::optional<uint64_t> shifted_sample_start(uint64_t start, uint64_t end, uint64_t sampleLength,
                                                       int32_t ticks, int32_t loopLength) {
	if (loopLength <= 0 || end < start)
		return std::nullopt;
	const uint64_t duration = end - start;
	const uint64_t magnitude = ticks < 0 ? -static_cast<int64_t>(ticks) : ticks;
	if (magnitude && duration > std::numeric_limits<uint64_t>::max() / magnitude)
		return std::nullopt;
	const uint64_t distance = duration * magnitude / static_cast<uint32_t>(loopLength);
	uint64_t result;
	if (ticks >= 0) {
		if (distance > start)
			return std::nullopt;
		result = start - distance;
	}
	else {
		if (distance > std::numeric_limits<uint64_t>::max() - start)
			return std::nullopt;
		result = start + distance;
	}
	if (result > sampleLength || duration > std::numeric_limits<uint64_t>::max() - result)
		return std::nullopt;
	return result;
}

} // namespace deluge::model
