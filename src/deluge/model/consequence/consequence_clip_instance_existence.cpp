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

#include "model/consequence/consequence_clip_instance_existence.h"
#include "definitions_cxx.hpp"
#include "model/clip/clip_instance.h"
#include "model/instrument/instrument.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "util/misc.h"

ConsequenceClipInstanceExistence::ConsequenceClipInstanceExistence(Output* newOutput, ClipInstance* clipInstance,
                                                                   ExistenceChangeType newType) {
	output = newOutput;
	clip = clipInstance->clip;
	pos = clipInstance->pos;
	length = clipInstance->length;

	type = newType;
}

Error ConsequenceClipInstanceExistence::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || !modelStack->song->owns_output_for_undo(output)) {
		return Error::BUG;
	}

	if (time == util::to_underlying(type)) { // (Re-)delete
		int32_t i = output->clipInstances.search(pos, GREATER_OR_EQUAL);
		if (i < 0 || i >= output->clipInstances.getNumElements()) {
			return Error::BUG;
		}
		auto* instance = output->clipInstances.getElement(i);
		if (!instance || instance->pos != pos || instance->clip != clip) {
			return Error::BUG;
		}
		output->clipInstances.deleteAtIndex(i);
	}

	else { // (Re-)create
		// Copy retained metadata before reservation can service callbacks.
		Song* const song = modelStack->song;
		Output* const targetOutput = output;
		Clip* const targetClip = clip;
		const int32_t targetPos = pos, targetLength = length;
		if (song != currentSong || !song->can_reference_clip_from_output(targetClip, targetOutput))
			return Error::BUG;
		auto* existing =
		    targetOutput->clipInstances.getElement(targetOutput->clipInstances.search(targetPos, GREATER_OR_EQUAL));
		if (existing && existing->pos == targetPos)
			return Error::BUG;

		using namespace deluge::gui::ui_session;
		const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
		const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
		bool reserved = targetOutput->clipInstances.ensureEnoughSpaceAllocated(1);
		if (currentSong != song || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
		    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
		    || !song->can_reference_clip_from_output(targetClip, targetOutput))
			return Error::BUG;
		if (!reserved)
			return Error::INSUFFICIENT_RAM;

		// Search again after reservation. Commit cannot allocate, even for wrapped
		// storage, so nothing can yield between this validation and initialization.
		int32_t i = targetOutput->clipInstances.search(targetPos, GREATER_OR_EQUAL);
		existing = targetOutput->clipInstances.getElement(i);
		if (existing && existing->pos == targetPos)
			return Error::BUG;
		Error error = targetOutput->clipInstances.insert_at_index_without_allocation(i);
		if (error != Error::NONE)
			return error;
		ClipInstance* clipInstance = targetOutput->clipInstances.getElement(i);
		clipInstance->pos = targetPos;
		clipInstance->length = targetLength;
		clipInstance->clip = targetClip;
	}

	return Error::NONE;
}
