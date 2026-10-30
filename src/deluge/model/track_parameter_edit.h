#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

enum class ActionType;

namespace deluge::model {

struct AutomationRegion {
	uint32_t pos;
	uint32_t length;
	ActionType actionType;

	// Preserve the same timeline positions; do not wrap edits into shorter clips.
	uint32_t lengthWithin(int32_t loopLength) const {
		if (loopLength <= 0 || pos >= static_cast<uint32_t>(loopLength)) {
			return 0;
		}
		return std::min(length, static_cast<uint32_t>(loopLength) - pos);
	}
};

// An explicit user edit, not an automation playback value. A region carries its
// undo action type; no region means the normal live knob/menu/MIDI edit path.
struct ParameterEdit {
	int32_t value = 0;
	bool shouldLogAction = true;
	bool mayDeleteNodesInLinearRun = true;
	bool isMPE = false;
	std::optional<AutomationRegion> region;
};

} // namespace deluge::model
