/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence_note_array_change.h"
#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

ConsequenceNoteArrayChange::ConsequenceNoteArrayChange(InstrumentClip* newClip, int32_t newNoteRowId,
                                                       NoteVector* newNoteVector, bool stealData) {
	type = Consequence::NOTE_ARRAY_CHANGE;
	clip = newClip;
	noteRowId = newNoteRowId;
	if (clip && clip->type == ClipType::INSTRUMENT) {
		if (auto* row = clip->getNoteRowFromId(noteRowId))
			note_row_identity = row->undo_identity;
	}

	// Either steal the data...
	if (stealData) {
		backedUpNoteVector.swapStateWith(newNoteVector);
	}

	// Or clone it...
	else {
		snapshot_valid_ = backedUpNoteVector.cloneFrom(newNoteVector);
	}
}

Error ConsequenceNoteArrayChange::revert(TimeType time, ModelStack* modelStack) {
	if (!snapshot_valid_)
		return Error::BUG;
	if (!modelStack || !modelStack->song || !modelStack->song->contains_clip_for_undo(clip)
	    || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;

	NoteRow* noteRow = clip->getNoteRowFromId(noteRowId);
	if (!noteRow || !note_row_identity || noteRow->undo_identity != note_row_identity) {
		return Error::BUG;
	}

	noteRow->notes.swapStateWith(&backedUpNoteVector);

	return Error::NONE;
}
