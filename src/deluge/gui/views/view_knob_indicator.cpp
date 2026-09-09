/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/led/indicator_leds.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "util/functions.h"
#include <algorithm>

namespace params = deluge::modulation::params;
using namespace deluge;
using namespace gui;

void View::setKnobIndicatorLevel(uint8_t whichModEncoder) {
	// timelineCounter and paramManager could be NULL - if the user is holding down an audition pad in Arranger,
	// and that Output has no Clips. Especially if it's a MIDIInstrument (no ParamManager).
	ModelStackWithAutoParam* modelStackWithParam =
	    activeModControllableModelStack.modControllable->getParamFromModEncoder(
	        whichModEncoder, &activeModControllableModelStack, false);

	int32_t knobPos;
	bool isBipolar = false;

	if (modelStackWithParam->autoParam
	    || (modelStackWithParam->paramCollection
	        && modelStackWithParam->paramCollection->has_current_value(modelStackWithParam->paramId))) {
		int32_t value = modelStackWithParam->autoParam
		                    ? modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam)
		                    : modelStackWithParam->paramCollection->get_current_value(modelStackWithParam->paramId);
		ParamCollection* paramCollection = modelStackWithParam->paramCollection;
		params::Kind kind = paramCollection->getParamKind();
		isBipolar = isParamBipolar(kind, modelStackWithParam->paramId);
		knobPos = paramCollection->paramValueToKnobPos(value, modelStackWithParam);
		int32_t lowerLimit;

		if (kind == params::Kind::PATCH_CABLE) {
			lowerLimit = std::min(-192_i32, knobPos);
		}
		else {
			lowerLimit = std::min(-64_i32, knobPos);
		}
		knobPos = std::clamp(knobPos, lowerLimit, 64_i32);
		if (isParamQuantizedStutter(kind, modelStackWithParam->paramId,
		                            (ModControllableAudio*)modelStackWithParam->modControllable)
		    && !isUIModeActive(UI_MODE_STUTTERING)) {
			if (knobPos < -39) { // 4ths stutter: no leds turned on
				knobPos = -64;
			}
			else if (knobPos < -14) { // 8ths stutter: 1 led turned on
				knobPos = -32;
			}
			else if (knobPos < 14) { // 16ths stutter: 2 leds turned on
				knobPos = 0;
			}
			else if (knobPos < 39) { // 32nds stutter: 3 leds turned on
				knobPos = 32;
			}
			else { // 64ths stutter: all 4 leds turned on
				knobPos = 64;
			}
		}
		knobPos += kKnobPosOffset;

		if (kind == params::Kind::PATCH_CABLE) {
			knobPos = view.convertPatchCableKnobPosToIndicatorLevel(knobPos);
		}
	}
	else {
		if (modelStackWithParam->paramId == 255) {
			knobPos = modelStackWithParam->modControllable->getKnobPosForNonExistentParam(whichModEncoder,
			                                                                              modelStackWithParam);
			knobPos += kKnobPosOffset;
		}
		// is it not just a param? then its a patch cable
		else if (!((modelStackWithParam->paramId & 0x0000FF00) == 0x0000FF00)) {
			// default value for patch cable
			// (equals 0 (midpoint) in -128 to +128 range)
			knobPos = 64;
			isBipolar = true;
		}
	}

	indicator_leds::setKnobIndicatorLevel(whichModEncoder, knobPos, isBipolar);
}

/// if you're dealing with a patch cable which has a -128 to +128 range
/// we'll need to convert it to a 0 - 128 range for purpose of rendering on knob indicators
int32_t View::convertPatchCableKnobPosToIndicatorLevel(int32_t knobPos) {
	int32_t newKnobPos = (knobPos + kMaxKnobPos) >> 1;
	// adjustment to make sure that when knobPos returned is 64, it's really 64
	// the knob LED indicator is centred around 64
	// so the knob pos returned from this function is used to blink the LED when it reaches 64
	// so to make sure it doesn't blink twice (e.g. when the value is 64 and in between 64 and 65)
	// we adjust it here so it only returns 64 once
	if (newKnobPos == 64 && knobPos != 0) {
		newKnobPos += knobPos;
	}

	return newKnobPos;
}
