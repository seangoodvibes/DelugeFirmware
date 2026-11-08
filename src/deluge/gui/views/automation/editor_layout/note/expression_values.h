#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace note_expression {

// X and Y are signed MPE dimensions. Keep a neutral pad at row four.
constexpr int32_t displayValueFromRow(int32_t row, bool bipolar) {
	constexpr int32_t signedValues[] = {-64, -48, -32, -16, 0, 16, 32, 64};
	constexpr int32_t pressureValues[] = {0, 18, 37, 55, 73, 91, 110, 128};
	return bipolar ? signedValues[row] : pressureValues[row];
}

constexpr int32_t valueFromRow(int32_t row, bool bipolar) {
	int64_t value = int64_t{displayValueFromRow(row, bipolar)} * (bipolar ? (1 << 25) : (1 << 24));
	return static_cast<int32_t>(std::min(value, int64_t{std::numeric_limits<int32_t>::max()}));
}

struct Region {
	int32_t start;
	int32_t end;
};

// Intersect the pad with the note, treating a wrapped tail as a note starting before zero.
constexpr Region editableRegion(int32_t noteStart, int32_t noteLength, int32_t loopLength, int32_t squareStart,
                                int32_t squareEnd) {
	if (noteStart >= squareEnd) {
		noteStart -= loopLength;
	}
	return {std::max(squareStart, noteStart), std::min({squareEnd, noteStart + noteLength, loopLength})};
}

} // namespace note_expression
