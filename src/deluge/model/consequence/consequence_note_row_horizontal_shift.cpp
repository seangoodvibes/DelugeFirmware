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

#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/action/reversible_shift.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

ConsequenceNoteRowHorizontalShift::ConsequenceNoteRowHorizontalShift(InstrumentClip* targetClip, int32_t newNoteRowId,
                                                                     int32_t newAmount, bool newShiftAutomation,
                                                                     bool newShiftSequenceAndMPE) {
	amount = newAmount;
	clip = targetClip;
	noteRowId = newNoteRowId;
	if (clip && clip->type == ClipType::INSTRUMENT) {
		if (auto* row = clip->getNoteRowFromId(noteRowId))
			note_row_identity = row->undo_identity;
	}
	shiftAutomation = newShiftAutomation;
	shiftSequenceAndMPE = newShiftSequenceAndMPE;
}

Error ConsequenceNoteRowHorizontalShift::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || !modelStack->song->contains_clip_for_undo(clip)
	    || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	NoteRow* row = clip->getNoteRowFromId(noteRowId);
	if (!row || !note_row_identity || row->undo_identity != note_row_identity || clip->loopLength <= 0
	    || row->loopLengthIfIndependent < 0)
		return Error::BUG;

	if (!deluge::model::is_reversible_shift(amount))
		return Error::BUG;
	int32_t amountNow = amount;

	if (time == BEFORE) {
		amountNow = -amountNow;
	}

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addTimelineCounter(clip)->addNoteRow(noteRowId, row);

	((InstrumentClip*)modelStackWithNoteRow->getTimelineCounter())
	    ->shiftOnlyOneNoteRowHorizontally(modelStackWithNoteRow, amountNow, shiftAutomation, shiftSequenceAndMPE);

	return Error::NONE;
}
