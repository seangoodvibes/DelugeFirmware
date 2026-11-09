// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>

// A bounded least common multiple of the clip and its note-row iteration periods.
// Refuse oversized periods instead of wrapping the integer or truncating the musical pattern.
class FreezeCycle {
public:
	explicit FreezeCycle(int32_t clipLength)
	    : ticks_(clipLength), clipLength_(clipLength), limit_(std::min<int64_t>(int64_t{clipLength} * 64, INT32_MAX)) {}
	bool include(int32_t rowLength, uint32_t iterations) {
		if (ticks_ <= 0 || rowLength <= 0 || iterations == 0) {
			return false;
		}
		int64_t rowPeriod = int64_t{rowLength} * iterations;
		int64_t factor = ticks_ / std::gcd(ticks_, rowPeriod);
		if (factor > limit_ / rowPeriod) {
			return false;
		}
		ticks_ = factor * rowPeriod;
		return true;
	}
	// Match the synced LFO clock: 3 * 2^(9-level) internal ticks per even cycle.
	// Keep the numerator for fractional dotted periods (e.g. 9/2 ticks), so an
	// integer clip boundary contains a whole number of LFO cycles without truncation.
	bool includeSyncedLfo(uint32_t level, uint32_t type) {
		if (level < 1 || level > 9) {
			return false;
		}
		int32_t numerator = 3 << (9 - level);
		int32_t denominator = 1;
		if (type == 10) { // SYNC_TYPE_TRIPLET
			numerator *= 2;
			denominator = 3;
		}
		else if (type == 19) { // SYNC_TYPE_DOTTED
			numerator *= 3;
			denominator = 2;
		}
		else if (type != 0) {
			return false;
		}
		return include(numerator / std::gcd(numerator, denominator), 1);
	}
	// Capture at least eight clip loops, rounded up to a complete known pattern.
	// If that exceeds the bound, keep the largest whole pattern which fits.
	bool extendForSnapshot(int64_t maxTicks = INT32_MAX) {
		if (ticks_ <= 0 || clipLength_ <= 0 || maxTicks < ticks_) {
			return false;
		}
		int64_t bound = std::min(limit_, maxTicks);
		int64_t target = std::min(bound, clipLength_ * 8);
		int64_t repeats = std::max<int64_t>(1, (target + ticks_ - 1) / ticks_);
		repeats = std::min(repeats, bound / ticks_);
		if (!repeats) {
			return false;
		}
		ticks_ *= repeats;
		return true;
	}
	int32_t ticks() const { return static_cast<int32_t>(ticks_); }

private:
	int64_t ticks_;
	int64_t clipLength_;
	int64_t limit_;
};
