/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence_clip_instance_change.h"
#include "definitions_cxx.hpp"
#include "model/clip/clip_instance.h"
#include "model/instrument/instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"

ConsequenceClipInstanceChange::ConsequenceClipInstanceChange(Output* newOutput, ClipInstance* clipInstance,
                                                             int32_t posAfter, int32_t lengthAfter, Clip* clipAfter) {
	Consequence::type = Consequence::CLIP_INSTANCE_CHANGE;
	output = newOutput;
	pos[BEFORE] = clipInstance->pos;
	pos[AFTER] = posAfter;
	length[BEFORE] = clipInstance->length;
	length[AFTER] = lengthAfter;
	clip[BEFORE] = clipInstance->clip;
	clip[AFTER] = clipAfter;
}

Error ConsequenceClipInstanceChange::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || modelStack->song != currentSong
	    || !modelStack->song->owns_output_for_undo(output)) {
		return Error::BUG;
	}

	int32_t i = output->clipInstances.search(pos[1 - time], GREATER_OR_EQUAL);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	// A later edit may retain the same position and clip while changing length.
	// Do not overwrite that edit with a stale undo/redo snapshot.
	if (!clipInstance || clipInstance->pos != pos[1 - time] || clipInstance->clip != clip[1 - time]
	    || clipInstance->length != length[1 - time]) {
		return Error::BUG;
	}
	if (!modelStack->song->can_reference_clip_from_output(clip[time], output)) {
		return Error::BUG;
	}
	// Another edit can occupy the old destination without changing this instance.
	// Keep the retained slot ordered and non-overlapping before committing any
	// field. Batch shifts visit instances in an order that preserves these bounds.
	auto* previous_instance = output->clipInstances.getElement(i - 1);
	auto* next_instance = output->clipInstances.getElement(i + 1);
	if ((previous_instance && static_cast<int64_t>(previous_instance->pos) + previous_instance->length > pos[time])
	    || (next_instance && static_cast<int64_t>(pos[time]) + length[time] > next_instance->pos)) {
		return Error::BUG;
	}
	clipInstance->pos = pos[time];
	clipInstance->length = length[time];
	clipInstance->clip = clip[time];

	return Error::NONE;
}
