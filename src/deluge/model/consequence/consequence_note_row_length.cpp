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

#include "model/consequence/consequence_note_row_length.h"
#include "gui/ui/ui_navigation_state.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

ConsequenceNoteRowLength::ConsequenceNoteRowLength(InstrumentClip* targetClip, int32_t newNoteRowId,
                                                   int32_t newLength) {
	clip = targetClip;
	noteRowId = newNoteRowId;
	if (clip && clip->type == ClipType::INSTRUMENT) {
		if (auto* row = clip->getNoteRowFromId(noteRowId))
			note_row_identity = row->undo_identity;
	}
	backedUpLength = newLength;
}

Error ConsequenceNoteRowLength::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || !modelStack->song->contains_clip_for_undo(clip)
	    || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	if (backedUpLength <= 0)
		return Error::BUG;
	NoteRow* row = clip->getNoteRowFromId(noteRowId);
	if (!row || !note_row_identity || row->undo_identity != note_row_identity || clip->loopLength <= 0
	    || row->loopLengthIfIndependent < 0)
		return Error::BUG;

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addTimelineCounter(clip)->addNoteRow(noteRowId, row);
	return performChange(modelStackWithNoteRow, nullptr, modelStackWithNoteRow->getLastProcessedPos(),
	                     modelStackWithNoteRow->getNoteRow()->hasIndependentPlayPos());
}

Error ConsequenceNoteRowLength::performChange(ModelStackWithNoteRow* modelStack, Action* actionToRecordTo,
                                              int32_t oldPos, // Sometimes needs overriding
                                              bool hadIndependentPlayPosBefore) {
	// UI edits call this directly rather than going through revert(). Validate
	// the retained target before reading effective length or dispatching edits.
	if (!modelStack || !modelStack->song || !modelStack->song->contains_clip_for_undo(clip)
	    || modelStack->getTimelineCounterAllowNull() != clip || clip->type != ClipType::INSTRUMENT
	    || modelStack->noteRowId != noteRowId || backedUpLength <= 0 || clip->loopLength <= 0)
		return Error::BUG;
	NoteRow* row = clip->getNoteRowFromId(noteRowId);
	if (!row || modelStack->getNoteRowAllowNull() != row || !note_row_identity
	    || row->undo_identity != note_row_identity || row->loopLengthIfIndependent < 0)
		return Error::BUG;

	// Snapshot values locally: callback checks must precede further target
	// dereferences and the saved-length exchange.
	Song* owner = modelStack->song;
	Song* active_song = currentSong;
	InstrumentClip* targetClip = clip;
	const int32_t target_row_id = noteRowId;
	const uint64_t target_identity = note_row_identity;
	const int32_t requested_length = backedUpLength;
	const int32_t parent_length = targetClip->loopLength;
	auto* targetOutput = targetClip->output;
	auto revision = [](deluge::gui::ui_session::Id id) {
		return deluge::gui::ui_session::navigation.for_owner(id).structural_refresh.revision();
	};
	const auto local_revision = revision(deluge::gui::ui_session::Id::Local);
	const auto remote_revision = revision(deluge::gui::ui_session::Id::Remote);
	int32_t prevLength = modelStack->getLoopLength();

	Error error = modelStack->getNoteRow()->setLength(modelStack, backedUpLength, actionToRecordTo, oldPos,
	                                                  hadIndependentPlayPosBefore);
	if (error != Error::NONE)
		return error;

	if (currentSong != active_song || modelStack->song != owner
	    || revision(deluge::gui::ui_session::Id::Local) != local_revision
	    || revision(deluge::gui::ui_session::Id::Remote) != remote_revision)
		return Error::BUG;
	if (!owner->contains_clip_for_undo(targetClip) || modelStack->getTimelineCounterAllowNull() != targetClip
	    || targetClip->type != ClipType::INSTRUMENT || targetClip->loopLength != parent_length
	    || targetClip->output != targetOutput || modelStack->noteRowId != target_row_id
	    || targetClip->getNoteRowFromId(target_row_id) != row || modelStack->getNoteRowAllowNull() != row
	    || row->undo_identity != target_identity || row->loopLengthIfIndependent < 0)
		return Error::BUG;
	if ((row->loopLengthIfIndependent ? row->loopLengthIfIndependent : parent_length) != requested_length)
		return Error::BUG;

	backedUpLength = prevLength;
	return Error::NONE;
}
