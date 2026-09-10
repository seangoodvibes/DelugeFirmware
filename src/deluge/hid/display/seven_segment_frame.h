#pragma once
#include "definitions_cxx.hpp"
#include <array>
#include <cstdint>

namespace deluge::hid::display {

struct SevenSegmentFrame {
	std::array<uint8_t, kNumericDisplayLength> segments{};
	uint32_t revision = 0;
	void publish(std::array<uint8_t, kNumericDisplayLength> rendered, uint8_t fixedDot = 255) {
		if (fixedDot < kNumericDisplayLength)
			rendered[fixedDot] |= 0x80;
		else if ((fixedDot & 0xf0) == 0x80) {
			for (size_t i = 0; i < rendered.size(); ++i) {
				if ((fixedDot >> (rendered.size() - 1 - i)) & 1)
					rendered[i] |= 0x80;
			}
		}
		segments = rendered;
		++revision;
	}
};

} // namespace deluge::hid::display
