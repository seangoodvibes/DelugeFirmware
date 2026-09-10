/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "gui/ui/ui_navigation_state.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include <memory>

ConsequenceInstrumentClipMultiply::ConsequenceInstrumentClipMultiply() {
	// TODO Auto-generated constructor stub
}

Error ConsequenceInstrumentClipMultiply::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song)
		return Error::BUG;
	Clip* currentClip = modelStack->song->getCurrentClip();
	if (!currentClip || !modelStack->song->contains_clip_for_undo(currentClip)
	    || currentClip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	InstrumentClip* clip = static_cast<InstrumentClip*>(currentClip);
	if (time == BEFORE) {
		// Validate all halved lengths before changing the parent clip. Zero
		// means an inherited row length; explicit lengths must remain positive.
		if (clip->loopLength < 2)
			return Error::BUG;
		for (int32_t i = 0; i < clip->noteRows.getNumElements(); ++i) {
			int32_t length = clip->noteRows.getElement(i)->loopLengthIfIndependent;
			if (length != 0 && length < 2)
				return Error::BUG;
		}
		Song* owner = modelStack->song;
		Song* active_song = currentSong;
		auto revision = [](deluge::gui::ui_session::Id id) {
			return deluge::gui::ui_session::navigation.for_owner(id).structural_refresh.revision();
		};
		const auto local_revision = revision(deluge::gui::ui_session::Id::Local);
		const auto remote_revision = revision(deluge::gui::ui_session::Id::Remote);
		const int32_t original_row_count = clip->noteRows.getNumElements();
		const int32_t original_length = clip->loopLength;
		auto* original_output = clip->output;
		auto context_valid = [&](int32_t expected_length) {
			if (currentSong != active_song || modelStack->song != owner
			    || revision(deluge::gui::ui_session::Id::Local) != local_revision
			    || revision(deluge::gui::ui_session::Id::Remote) != remote_revision)
				return false;
			if (!owner->contains_clip_for_undo(clip) || owner->getCurrentClip() != clip)
				return false;
			return clip->type == ClipType::INSTRUMENT && clip->loopLength == expected_length
			       && clip->output == original_output && clip->noteRows.getNumElements() == original_row_count;
		};
		struct row_snapshot {
			NoteRow* address;
			uint64_t identity;
			int32_t length;
		};
		if (original_row_count < 0 || static_cast<uint32_t>(original_row_count) > UINT32_MAX / sizeof(row_snapshot))
			return Error::BUG;
		auto free_snapshot = [](row_snapshot* rows) {
			if (rows)
				delugeDealloc(rows);
		};
		std::unique_ptr<row_snapshot, decltype(free_snapshot)> saved_rows(nullptr, free_snapshot);
		if (original_row_count) {
			saved_rows.reset(static_cast<row_snapshot*>(
			    GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(row_snapshot) * original_row_count)));
			if (!context_valid(original_length))
				return Error::BUG;
			if (!saved_rows)
				return Error::INSUFFICIENT_RAM;
			for (int32_t row_index = 0; row_index < original_row_count; ++row_index) {
				auto* row = clip->noteRows.getElement(row_index);
				if (row->loopLengthIfIndependent != 0 && row->loopLengthIfIndependent < 2)
					return Error::BUG;
				saved_rows.get()[row_index] = {row, row->undo_identity, row->loopLengthIfIndependent};
			}
		}
		const int32_t target_length = original_length >> 1;
		if (!owner->setClipLength(clip, target_length, nullptr))
			return Error::BUG;

		// Check external context before touching a target that callbacks may have removed.
		if (!context_valid(target_length))
			return Error::BUG;

		for (int32_t row_index = 0; row_index < original_row_count; ++row_index) {
			auto* row = clip->noteRows.getElement(row_index);
			const auto& saved_row = saved_rows.get()[row_index];
			// lengthChanged may convert a row matching the new parent length to inherited.
			const bool inherited_length = saved_row.length == target_length && row->loopLengthIfIndependent == 0;
			if (row != saved_row.address || row->undo_identity != saved_row.identity
			    || (row->loopLengthIfIndependent != saved_row.length && !inherited_length))
				return Error::BUG;
		}

		// Deal with any NoteRows with independent length.
		return clip->halveNoteRowsWithIndependentLength(modelStack->addTimelineCounter(clip));
	}
	else {
		if (!modelStack->song->doubleClipLength(clip))
			return Error::BUG;
	}

	return Error::NONE;
}
