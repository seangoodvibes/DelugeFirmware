/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdint>
#include <string_view>

namespace deluge::notes {
// Signed semitone offsets stored in an unsigned byte; zero remains natural.
constexpr uint8_t encodeAccidental(int32_t semitones) {
	return static_cast<uint8_t>(semitones);
}
constexpr int32_t decodeAccidental(uint8_t value) {
	return value < 128 ? value : static_cast<int32_t>(value) - 256;
}

// Older record formats have no accidental byte. The extended format appends it after the 28 existing hex digits.
constexpr uint8_t accidentalFromNoteData(std::string_view record) {
	if (record.size() != 30)
		return 0;
	auto digit = [](char c) -> int32_t {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		return -1;
	};
	int32_t high = digit(record[28]), low = digit(record[29]);
	return high < 0 || low < 0 ? 0 : static_cast<uint8_t>(high * 16 + low);
}

// Lower note wins ties. A predicate can exclude occupied recording rows as well as notes outside the scale.
template <typename Allowed>
int32_t nearestScaleNote(int32_t played, Allowed allowed) {
	for (int32_t distance = 0; distance < 128; ++distance) {
		if (played - distance >= 0 && allowed(played - distance))
			return played - distance;
		if (distance && played + distance <= 127 && allowed(played + distance))
			return played + distance;
	}
	return -1;
}
template <typename Allowed>
int32_t noteForRecording(int32_t played, bool inScaleMode, bool recordAccidental, Allowed allowed) {
	return inScaleMode && recordAccidental ? nearestScaleNote(played, allowed) : played;
}
} // namespace deluge::notes
