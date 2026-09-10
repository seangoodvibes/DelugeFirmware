#pragma once

#include <algorithm>
#include <cstdint>

namespace deluge::hid::encoders {

// One history per encoder and UI session; physical IRQ accumulation is separate.
class EncoderAcceleration {
public:
	double advance(int8_t offset, double time) {
		const double elapsed = time - last_time_;
		if (elapsed >= 0.3 || elapsed < 0.0 || offset != last_offset_) {
			speed_ = 0.0;
		}
		else if (elapsed > 0.0) {
			speed_ = speed_ * 0.9 + 0.1 / elapsed;
		}
		last_time_ = time;
		last_offset_ = offset;
		return std::clamp(speed_ * 0.15, 1.0, 3.0);
	}

private:
	double speed_ = 0.0;
	double last_time_ = 0.0;
	int8_t last_offset_ = 0;
};

} // namespace deluge::hid::encoders
