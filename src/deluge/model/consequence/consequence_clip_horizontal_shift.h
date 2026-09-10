/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "model/action/reversible_shift.h"
#include "model/consequence/consequence.h"
#include <cstdint>
#include <limits>

class Clip;

class ConsequenceClipHorizontalShift final : public Consequence {
public:
	ConsequenceClipHorizontalShift(Clip* targetClip, int32_t newAmount, bool newShiftAutomation,
	                               bool newShiftSequenceAndMPE);
	Error revert(TimeType time, ModelStack* modelStack) override;
	bool can_accumulate(Clip* target, int32_t delta, bool automation, bool sequence_and_mpe) const {
		const int64_t combined = static_cast<int64_t>(amount) + delta;
		return clip == target && shiftAutomation == automation && shiftSequenceAndMPE == sequence_and_mpe
		       && deluge::model::is_reversible_shift(combined);
	}

	int32_t amount;
	bool shiftAutomation;
	bool shiftSequenceAndMPE;
	Clip* clip;
};
