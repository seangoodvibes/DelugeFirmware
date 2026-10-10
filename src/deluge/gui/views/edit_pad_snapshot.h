#pragma once

#include <cstddef>
#include <iterator>

namespace deluge::gui {

// Snapshot metadata only. Empty records may have uninitialized pointers; do not read them.
template <typename Press>
Press capture_edit_pad_snapshot(const Press& press) {
	Press saved{};
	saved.isActive = press.isActive;
	saved.gesture_revision = press.gesture_revision;
	if (!press.isActive) {
		return saved;
	}
	saved.xDisplay = press.xDisplay;
	saved.yDisplay = press.yDisplay;
	saved.deleteOnDepress = press.deleteOnDepress;
	saved.deleteOnScroll = press.deleteOnScroll;
	saved.isBlurredSquare = press.isBlurredSquare;
	saved.mpeCachedYet = press.mpeCachedYet;
	saved.intendedPos = press.intendedPos;
	saved.intendedLength = press.intendedLength;
	saved.intendedVelocity = press.intendedVelocity;
	saved.intendedProbability = press.intendedProbability;
	saved.intendedIterance = press.intendedIterance;
	saved.intendedFill = press.intendedFill;
	for (size_t dimension = 0; dimension < std::size(press.stolenMPE); ++dimension) {
		saved.stolenMPE[dimension].num = press.stolenMPE[dimension].num;
		if (press.stolenMPE[dimension].num) {
			saved.stolenMPE[dimension].nodes = press.stolenMPE[dimension].nodes;
		}
	}
	return saved;
}

template <typename Press>
bool matches_edit_pad_snapshot(const Press& press, const Press& saved) {
	if (press.isActive != saved.isActive || press.gesture_revision != saved.gesture_revision) {
		return false;
	}
	if (!press.isActive) {
		return true;
	}
	if (press.xDisplay != saved.xDisplay || press.yDisplay != saved.yDisplay
	    || press.deleteOnDepress != saved.deleteOnDepress || press.deleteOnScroll != saved.deleteOnScroll
	    || press.isBlurredSquare != saved.isBlurredSquare || press.mpeCachedYet != saved.mpeCachedYet
	    || press.intendedPos != saved.intendedPos || press.intendedLength != saved.intendedLength
	    || press.intendedVelocity != saved.intendedVelocity || press.intendedProbability != saved.intendedProbability
	    || press.intendedIterance != saved.intendedIterance || press.intendedFill != saved.intendedFill) {
		return false;
	}
	for (size_t dimension = 0; dimension < std::size(press.stolenMPE); ++dimension) {
		if (press.stolenMPE[dimension].num != saved.stolenMPE[dimension].num
		    || (press.stolenMPE[dimension].num
		        && press.stolenMPE[dimension].nodes != saved.stolenMPE[dimension].nodes)) {
			return false;
		}
	}
	return true;
}

} // namespace deluge::gui
