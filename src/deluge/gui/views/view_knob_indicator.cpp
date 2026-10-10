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
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "util/functions.h"
#include <algorithm>

namespace params = deluge::modulation::params;
using namespace deluge;
using namespace gui;

void View::setKnobIndicatorLevel(uint8_t whichModEncoder) {
	if (!activeModControllableModelStack.modControllable)
		return;
	const auto source_owner = deluge::gui::ui_session::current();
	deluge::gui::ui_session::Scope owner_scope(source_owner);
	auto* const source_song = currentSong;
	auto* const source_controllable = activeModControllableModelStack.modControllable;
	auto* const source_manager = activeModControllableModelStack.paramManager;
	auto* const source_timeline = activeModControllableModelStack.getTimelineCounterAllowNull();
	const auto source_position = modPos;
	const auto source_length = modLength;
	const auto source_note_row = modNoteRowId;
	const auto context_matches = [&] {
		return deluge::gui::ui_session::current() == source_owner && currentSong == source_song
		       && activeModControllableModelStack.modControllable == source_controllable
		       && activeModControllableModelStack.paramManager == source_manager
		       && activeModControllableModelStack.getTimelineCounterAllowNull() == source_timeline
		       && modPos == source_position && modLength == source_length && modNoteRowId == source_note_row;
	};
	// timelineCounter and paramManager could be NULL - if the user is holding down an audition pad in Arranger,
	// and that Output has no Clips. Especially if it's a MIDIInstrument (no ParamManager).
	ModelStackWithAutoParam* modelStackWithParam =
	    activeModControllableModelStack.modControllable->getParamFromModEncoder(
	        whichModEncoder, &activeModControllableModelStack, false);

	if (!context_matches() || !modelStackWithParam)
		return;

	auto* const source_collection = modelStackWithParam->paramCollection;
	auto* const source_param = modelStackWithParam->autoParam;
	auto* const source_parameter_controllable = modelStackWithParam->modControllable;
	const auto source_param_id = modelStackWithParam->paramId;
	const auto parameter_matches = [&] {
		return context_matches() && modelStackWithParam->paramCollection == source_collection
		       && modelStackWithParam->autoParam == source_param && modelStackWithParam->paramId == source_param_id
		       && modelStackWithParam->modControllable == source_parameter_controllable;
	};

	int32_t knobPos = 0; // An unavailable plain parameter leaves the indicator off.
	bool isBipolar = false;

	const bool has_value = source_collection && (source_param || source_collection->has_current_value(source_param_id));
	if (!parameter_matches())
		return;
	if (has_value) {
		int32_t value = modelStackWithParam->autoParam
		                    ? modelStackWithParam->autoParam->getValuePossiblyAtPos(modPos, modelStackWithParam)
		                    : modelStackWithParam->paramCollection->get_current_value(modelStackWithParam->paramId);
		if (!parameter_matches())
			return;
		ParamCollection* paramCollection = modelStackWithParam->paramCollection;
		params::Kind kind = paramCollection->getParamKind();
		if (!parameter_matches())
			return;
		isBipolar = isParamBipolar(kind, modelStackWithParam->paramId);
		knobPos = paramCollection->paramValueToKnobPos(value, modelStackWithParam);
		if (!parameter_matches())
			return;
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
			knobPos = view_for_session().convertPatchCableKnobPosToIndicatorLevel(knobPos);
		}
	}
	else {
		if (modelStackWithParam->paramId == 255) {
			if (modelStackWithParam->modControllable) {
				knobPos = modelStackWithParam->modControllable->getKnobPosForNonExistentParam(whichModEncoder,
				                                                                              modelStackWithParam);
				knobPos += kKnobPosOffset;
			}
		}
		// is it not just a param? then its a patch cable
		else if (!((modelStackWithParam->paramId & 0x0000FF00) == 0x0000FF00)) {
			// default value for patch cable
			// (equals 0 (midpoint) in -128 to +128 range)
			knobPos = 64;
			isBipolar = true;
		}
	}

	if (parameter_matches())
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
