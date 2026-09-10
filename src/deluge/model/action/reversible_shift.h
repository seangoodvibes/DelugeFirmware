#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace deluge::model {

// Undo negates the stored movement, so INT32_MIN is not a valid amount.
constexpr bool is_reversible_shift(int64_t amount) {
	return amount >= -std::numeric_limits<int32_t>::max() && amount <= std::numeric_limits<int32_t>::max();
}

constexpr std::optional<int32_t> horizontal_shift_amount(int32_t delta, int32_t left, int32_t right) {
	const int64_t width = static_cast<int64_t>(right) - left;
	if (width <= 0)
		return std::nullopt;
	// A difference of two int32_t positions times an int32_t delta fits int64_t.
	const int64_t amount = width * delta;
	if (!is_reversible_shift(amount))
		return std::nullopt;
	return static_cast<int32_t>(amount);
}

} // namespace deluge::model
