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

#include "model/clip/clip_instance.h"
#include "gui/ui/ui_navigation_state.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_clip_instance_change.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "util/functions.h"
#include <new>

ClipInstance::ClipInstance() {
	// TODO Auto-generated constructor stub
}

RGB ClipInstance::getColour() {
	if (!clip || clip->isArrangementOnlyClip()) {
		return RGB::monochrome(128);
	}

	return defaultClipSectionColours[clip->section];
}

bool ClipInstance::change(Action* action, Output* output, int32_t new_pos, int32_t new_length, Clip* new_clip) {
	if (action) {
		Song* const song = currentSong;
		if (!song || !output || !song->owns_output_for_undo(output) || !action->action_identity)
			return false;
		const uint64_t action_identity = action->action_identity;
		const bool retained_in_history = actionLogger.firstAction[BEFORE] == action;
		const int32_t original_pos = pos, original_length = length;
		Clip* const original_clip = clip;
		const bool registered_original = original_clip && song->contains_clip_for_undo(original_clip);
		const bool registered_new = new_clip && song->contains_clip_for_undo(new_clip);
		using namespace deluge::gui::ui_session;
		const auto owner = current();
		const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
		const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
		void* consequence_memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipInstanceChange));
		if (currentSong != song || current() != owner
		    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
		    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
		    || (retained_in_history && actionLogger.firstAction[BEFORE] != action)
		    || action->action_identity != action_identity || !song->owns_output_for_undo(output)
		    || (registered_original && !song->contains_clip_for_undo(original_clip))
		    || (registered_new && !song->contains_clip_for_undo(new_clip))) {
			if (consequence_memory)
				delugeDealloc(consequence_memory);
			return false;
		}
		// Batch shifts can temporarily leave positions unsorted. Locate retained
		// storage without binary search or dereferencing the old pointer.
		ClipInstance* live_instance = nullptr;
		for (int32_t index = 0; index < output->clipInstances.getNumElements(); ++index) {
			auto* candidate = output->clipInstances.getElement(index);
			if (candidate == this) {
				live_instance = candidate;
				break;
			}
		}
		if (!live_instance || live_instance != this || live_instance->pos != original_pos
		    || live_instance->length != original_length || live_instance->clip != original_clip) {
			if (consequence_memory)
				delugeDealloc(consequence_memory);
			return false;
		}
		if (consequence_memory) {
			action->addConsequence(new (consequence_memory) ConsequenceClipInstanceChange(
			    output, live_instance, new_pos, new_length, new_clip));
		}
		else if (action->require_complete_snapshots) {
			action->snapshot_failed();
			return false;
		}
	}
	pos = new_pos;
	length = new_length;
	clip = new_clip;
	return true;
}
