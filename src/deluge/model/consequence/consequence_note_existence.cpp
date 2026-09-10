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

#include "model/consequence/consequence_note_existence.h"
#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/note/note_vector.h"
#include "model/song/song.h"
#include "util/misc.h"

ConsequenceNoteExistence::ConsequenceNoteExistence(InstrumentClip* newClip, int32_t newNoteRowId, Note* note,
                                                   ExistenceChangeType newType) {
	clip = newClip;
	noteRowId = newNoteRowId;
	if (clip && clip->type == ClipType::INSTRUMENT) {
		if (auto* row = clip->getNoteRowFromId(noteRowId))
			note_row_identity = row->undo_identity;
	}
	pos = note->pos;
	length = note->getLength();
	velocity = note->getVelocity();
	probability = note->getProbability();
	lift = note->getLift();
	iterance = note->getIterance();
	fill = note->getFill();

	type = newType;
}

Error ConsequenceNoteExistence::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || !modelStack->song->contains_clip_for_undo(clip)
	    || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	NoteRow* noteRow = clip->getNoteRowFromId(noteRowId);
	if (!noteRow || !note_row_identity || noteRow->undo_identity != note_row_identity) {
		return Error::BUG;
	}

	if (time == util::to_underlying(type)) {
		// Delete a note now
		int32_t i = noteRow->notes.search(pos, GREATER_OR_EQUAL);
		if (i < 0 || i >= noteRow->notes.getNumElements() || noteRow->notes.getElement(i)->pos != pos) {
			return Error::NONE; // This can happen, and is fine, when redoing a "Clip multiply" action with notes with
			                    // iteration dependence
		}
		noteRow->notes.deleteAtIndex(i);
	}
	else {
		// Reserve before committing. Reservation may service callbacks: retain
		// value metadata and re-resolve the live row before touching its storage.
		Song* const song = modelStack->song;
		InstrumentClip* const targetClip = clip;
		const int32_t target_row_id = noteRowId;
		const uint64_t target_identity = note_row_identity;
		Note restored;
		restored.pos = pos;
		restored.setLength(length);
		restored.setVelocity(velocity);
		restored.setProbability(probability);
		restored.setLift(lift);
		restored.setIterance(iterance);
		restored.setFill(fill);
		if (song != currentSong)
			return Error::BUG;
		bool reserved = noteRow->notes.ensureEnoughSpaceAllocated(1);
		if (currentSong != song || !song->contains_clip_for_undo(targetClip)
		    || targetClip->type != ClipType::INSTRUMENT)
			return Error::BUG;
		noteRow = targetClip->getNoteRowFromId(target_row_id);
		if (!noteRow || noteRow->undo_identity != target_identity)
			return Error::BUG;
		if (!reserved)
			return Error::INSUFFICIENT_RAM;
		int32_t i = noteRow->notes.search(restored.pos, GREATER_OR_EQUAL);
		auto* existing = noteRow->notes.getElement(i);
		if (existing && existing->pos == restored.pos)
			return Error::BUG;
		// No allocation or yield between validation and initialization.
		Error error = noteRow->notes.insert_at_index_without_allocation(i);
		if (error != Error::NONE)
			return error;
		*noteRow->notes.getElement(i) = restored;
	}

	return Error::NONE;
}
