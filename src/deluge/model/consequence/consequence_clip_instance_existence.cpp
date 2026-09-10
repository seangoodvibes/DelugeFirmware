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
	Consequence::type = Consequence::CLIP_INSTANCE_EXISTENCE;
	output = newOutput;
	clip = clipInstance->clip;
	pos = clipInstance->pos;
	length = clipInstance->length;

	type = newType;
}

Error ConsequenceClipInstanceExistence::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || modelStack->song != currentSong
	    || !modelStack->song->owns_output_for_undo(output)) {
		return Error::BUG;
	}

	if (time == util::to_underlying(type)) { // (Re-)delete
		int32_t i = output->clipInstances.search(pos, GREATER_OR_EQUAL);
		if (i < 0 || i >= output->clipInstances.getNumElements()) {
			return Error::BUG;
		}
		auto* instance = output->clipInstances.getElement(i);
		if (!instance || instance->pos != pos || instance->clip != clip || instance->length != length) {
			return Error::BUG;
		}
		output->clipInstances.delete_at_index_preserving_capacity(i);
	}

	else { // (Re-)create
		// Copy retained metadata before reservation can service callbacks.
		Song* const song = modelStack->song;
		Output* const target_output = output;
		Clip* const target_clip = clip;
		const int32_t target_pos = pos, target_length = length;
		if (song != currentSong || !song->can_reference_clip_from_output(target_clip, target_output))
			return Error::BUG;
		auto slot_is_available = [&] {
			if (target_length <= 0)
				return false;
			auto& instances = target_output->clipInstances;
			const int32_t index = instances.search(target_pos, GREATER_OR_EQUAL);
			auto* next_instance = instances.getElement(index);
			auto* previous_instance = instances.getElement(index - 1);
			return (!next_instance || static_cast<int64_t>(target_pos) + target_length <= next_instance->pos)
			       && (!previous_instance
			           || static_cast<int64_t>(previous_instance->pos) + previous_instance->length <= target_pos);
		};
		if (!slot_is_available())
			return Error::BUG;

		using namespace deluge::gui::ui_session;
		const auto owner = current();
		const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
		const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
		bool reserved = target_output->clipInstances.ensureEnoughSpaceAllocated(1);
		if (currentSong != song || modelStack->song != song || current() != owner
		    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
		    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
		    || !song->can_reference_clip_from_output(target_clip, target_output))
			return Error::BUG;
		if (!reserved)
			return Error::INSUFFICIENT_RAM;

		// Search again after reservation. Commit cannot allocate, even for wrapped
		// storage, so nothing can yield between this validation and initialization.
		int32_t i = target_output->clipInstances.search(target_pos, GREATER_OR_EQUAL);
		if (!slot_is_available())
			return Error::BUG;
		Error error = target_output->clipInstances.insert_at_index_without_allocation(i);
		if (error != Error::NONE)
			return error;
		ClipInstance* clipInstance = target_output->clipInstances.getElement(i);
		clipInstance->pos = target_pos;
		clipInstance->length = target_length;
		clipInstance->clip = target_clip;
	}

	return Error::NONE;
}
