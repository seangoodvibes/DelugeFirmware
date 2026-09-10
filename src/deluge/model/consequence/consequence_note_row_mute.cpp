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

#include "model/consequence/consequence_note_row_mute.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

ConsequenceNoteRowMute::ConsequenceNoteRowMute(InstrumentClip* newClip, int32_t newNoteRowId) {
	noteRowId = newNoteRowId;
	clip = newClip;
	if (clip && clip->type == ClipType::INSTRUMENT) {
		if (auto* row = clip->getNoteRowFromId(noteRowId))
			note_row_identity = row->undo_identity;
	}
}

Error ConsequenceNoteRowMute::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || !modelStack->song->contains_clip_for_undo(clip)
	    || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	NoteRow* noteRow = clip->getNoteRowFromId(noteRowId);
	if (!noteRow || !note_row_identity || noteRow->undo_identity != note_row_identity) {
		return Error::BUG;
	}

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addTimelineCounter(clip)->addNoteRow(noteRowId, noteRow);

	// Call this instead of Clip::toggleNoteRowMute(), cos that'd go and log another Action
	noteRow->toggleMute(modelStackWithNoteRow,
	                    playbackHandler.isEitherClockActive() && modelStackWithNoteRow->song->isClipActive(clip));

	return Error::NONE;
}
