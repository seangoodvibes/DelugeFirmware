// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dsp/stereo_sample.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

// Advisory only. A recorded profile can schedule an actual-audio comparison,
// never authorize a handoff. Bounded storage, no voices or engine snapshots.
class FreezeTransitionPlan {
public:
	static constexpr int32_t capacity = 256;
	static constexpr int32_t quantum = 2205; // 50 ms at the recorded rate.
	struct Feature {
		float power = 0;
		float bands[4]{};
	};
	void reset(int64_t expectedSamples = 0) {
		// Old bins need not be cleared: only freshly completed bins are visible.
		samples_ = inBin_ = count_ = 0;
		overflow_ = ready_ = false;
		sum_ = {};
		for (auto& channel : lowPass_)
			std::fill_n(channel, 3, 0);
		binSamples_ = std::max<int64_t>(quantum, (expectedSamples + capacity - 1) / capacity);
	}
	void capture(std::span<const StereoSample> audio, bool stereo = true) {
		if (overflow_)
			return;
		constexpr float coefficients[3] = {0.028093f, 0.107735f, 0.366131f};
		for (auto frame : audio) {
			if (count_ == capacity) {
				overflow_ = true;
				return;
			}
			for (int channel = 0; channel < 2; ++channel) {
				float value = stereo ? (channel ? frame.r : frame.l) : (int64_t{frame.l} + frame.r) / 2;
				sum_.power += value * value;
				auto& lows = lowPass_[channel];
				for (int b = 0; b < 3; ++b)
					lows[b] += coefficients[b] * (value - lows[b]);
				float bands[4] = {lows[0], lows[1] - lows[0], lows[2] - lows[1], value - lows[2]};
				for (int b = 0; b < 4; ++b)
					sum_.bands[b] += bands[b] * bands[b];
			}
			++samples_;
			if (++inBin_ == binSamples_)
				finishBin();
		}
	}
	void finish(int64_t actualSamples) {
		if (inBin_ && count_ < capacity)
			finishBin();
		ready_ = !overflow_ && samples_ == actualSamples && count_ >= 2;
	}
	bool ready() const { return ready_; }
	// Find a candidate within the next second at the *existing musical phase*.
	// Broad screening tolerances allow for bin averaging and future live changes.
	// Prefer the earliest compatible window with no large upcoming power change.
	int32_t nextWindow(int64_t position, float power, const float* bands) const {
		if (!ready_ || position < 0 || position >= samples_)
			return -1;
		float total = 0;
		for (int b = 0; b < 4; ++b)
			total += bands[b];
		for (int32_t offset = 0; offset <= 44100; offset += quantum) {
			int64_t at = position + offset;
			// Exclude wrap and filter-start transients; actual playback crossfades
			// differ from the recorded source in these regions.
			if (at < 11025 || at + 22050 >= samples_)
				continue;
			auto& target = bins_[at / binSamples_];
			auto& next = bins_[(at + 11025) / binSamples_];
			if (power < target.power * .5f || power > target.power * 2.f)
				continue;
			if (next.power < target.power * .5f || next.power > target.power * 2.f)
				continue;
			float targetTotal = 0;
			for (float band : target.bands)
				targetTotal += band;
			bool compatible = true;
			for (int b = 0; b < 4; ++b) {
				float expected = target.bands[b] / std::max(1.f, targetTotal);
				float actual = bands[b] / std::max(1.f, total);
				if (expected > .0001f && (actual < expected * .4f || actual > expected * 2.5f))
					compatible = false;
			}
			if (compatible)
				return offset;
		}
		return -1;
	}

private:
	void finishBin() {
		auto& bin = bins_[count_++];
		bin.power = sum_.power / inBin_;
		for (int b = 0; b < 4; ++b)
			bin.bands[b] = sum_.bands[b] / inBin_;
		sum_ = {};
		inBin_ = 0;
	}
	std::array<Feature, capacity> bins_{};
	Feature sum_{};
	float lowPass_[2][3]{};
	int64_t samples_ = 0;
	int32_t binSamples_ = quantum, inBin_ = 0, count_ = 0;
	bool overflow_ = false, ready_ = false;
};
