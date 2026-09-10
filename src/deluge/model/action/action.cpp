#include "gui/ui/ui_navigation_state.h"
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

#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_clip_state.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence.h"
#include "model/consequence/consequence_audio_clip_set_sample.h"
#include "model/consequence/consequence_clip_existence.h"
#include "model/consequence/consequence_clip_instance_existence.h"
#include "model/consequence/consequence_clip_length.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_existence.h"
#include "model/consequence/consequence_param_change.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "util/functions.h"
#include <cstdint>
#include <new>

Action::Action(ActionType newActionType) {
	firstConsequence = nullptr;
	nextAction = nullptr;
	type = newActionType;
	openForAdditions = true;

	clipStates = nullptr;
	numClipStates = 0;
	creationTime = AudioEngine::audioSampleTimer;

	offset = 0;
}

EnumStringMap<ActionType, 28> actionTypeMap{
    {{{ActionType::MISC, "misc"},
      {ActionType::NOTE_EDIT, "note_edit"},
      {ActionType::NOTE_TAIL_EXTEND, "note_tail_extend"},
      {ActionType::CLIP_LENGTH_INCREASE, "clip_length_increase"},
      {ActionType::CLIP_LENGTH_DECREASE, "clip_length_decrease"},
      {ActionType::RECORD, "record"},
      {ActionType::AUTOMATION_DELETE, "automation_delete"},
      {ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE, "param_unautomated_value_change"},
      {ActionType::SWING_CHANGE, "swing_change"},
      {ActionType::TEMPO_CHANGE, "tempo_change"},
      {ActionType::CLIP_MULTIPLY, "clip_multiply"},
      {ActionType::CLIP_CLEAR, "clip_clear"},
      {ActionType::CLIP_DELETE, "clip_delete"},
      {ActionType::NOTES_PASTE, "notes_paste"},
      {ActionType::PATTERN_PASTE, "pattern_paste"},
      {ActionType::AUTOMATION_PASTE, "automation_paste"},
      {ActionType::CLIP_INSTANCE_EDIT, "clip_instance_edit"},
      {ActionType::ARRANGEMENT_TIME_EXPAND, "arrangement_time_expand"},
      {ActionType::ARRANGEMENT_TIME_CONTRACT, "arrangement_time_contract"},
      {ActionType::ARRANGEMENT_CLEAR, "arrangement_clear"},
      {ActionType::ARRANGEMENT_RECORD, "arrangement_record"},
      {ActionType::CLIP_HORIZONTAL_SHIFT, "clip_horizontal_shift"},
      {ActionType::NOTE_NUDGE, "note_nudge"},
      {ActionType::NOTE_REPEAT_EDIT, "note_repeat_edit"},
      {ActionType::EUCLIDEAN_NUM_EVENTS_EDIT, "euclidean_num_events_edit"},
      {ActionType::NOTEROW_ROTATE, "noterow_rotate"},
      {ActionType::NOTEROW_LENGTH_EDIT, "noterow_length_edit"},
      {ActionType::NOTEROW_HORIZONTAL_SHIFT, "noterow_horizontal_shift"}}}};

// Call this before the destructor!
void Action::prepareForDestruction(int32_t whichQueueActionIn, Song* song) {
	// Snapshot storage belongs to this cleanup from here onward. Reentrant observers
	// must not see it as a live navigation snapshot while consequences are destroyed.
	ActionClipState* states_to_delete = clipStates;
	clipStates = nullptr;
	numClipStates = 0;
	openForAdditions = false;

	deleteAllConsequences(whichQueueActionIn, song);

	if (states_to_delete) {
		delugeDealloc(states_to_delete);
	}
}

void Action::deleteAllConsequences(int32_t whichQueueActionIn, Song* song) {
	Consequence* currentConsequence = firstConsequence;
	// Detach before audio servicing or consequence callbacks can yield. The action
	// must not expose nodes that this cleanup owns and may already have destroyed.
	firstConsequence = nullptr;
	while (currentConsequence) {
		AudioEngine::routineWithClusterLoading();
		Consequence* toDelete = currentConsequence;
		currentConsequence = currentConsequence->next;
		toDelete->prepareForDestruction(whichQueueActionIn, song);
		toDelete->~Consequence();
		delugeDealloc(toDelete);
	}
}

void Action::addConsequence(Consequence* consequence) {
	consequence->next = firstConsequence;
	firstConsequence = consequence;
}

// Returns error code
Error Action::revert(TimeType time, ModelStack* modelStack) {
	if (!modelStack || !modelStack->song || modelStack->song != currentSong)
		return Error::BUG;
	Song* const song = modelStack->song;
	const auto context_changed = [song, modelStack] { return currentSong != song || modelStack->song != song; };
	Consequence* thisConsequence = firstConsequence;

	// If we're a record-arrangement-from-session Action, there's a trick - we know that whether we're being undone or
	// redone, this will involve clearing the arrangement to the right of a certain pos. So we'll do that, and we'll
	// record the Consequences involved in doing so, so that this Action can then be reverted in the opposite direction
	// next time
	if (type == ActionType::ARRANGEMENT_RECORD) {
		firstConsequence = nullptr;
		Error clear_error = modelStack->song->clearArrangementBeyondPos(posToClearArrangementFrom, this);
		if (clear_error != Error::NONE || context_changed()) {
			// Preserve both sets for the caller's failure cleanup. Do not apply
			// old consequences against an arrangement that was only partly cleared.
			Consequence** tail = &firstConsequence;
			while (*tail)
				tail = &(*tail)->next;
			*tail = thisConsequence;
			return context_changed() ? Error::BUG : clear_error;
		}
		time = BEFORE;
	}

	Consequence* newFirstConsequence = nullptr;

	Error error = Error::NONE;

	while (thisConsequence) {
		if (context_changed()) {
			// Keep both processed and pending history reachable. In particular,
			// do not destroy arrangement consequences using a changed song.
			Consequence** tail = &thisConsequence;
			while (*tail)
				tail = &(*tail)->next;
			*tail = type == ActionType::ARRANGEMENT_RECORD ? firstConsequence : newFirstConsequence;
			firstConsequence = thisConsequence;
			return Error::BUG;
		}

		if (error == Error::NONE) {

			// Can't quite remember why, but we don't wanna revert param changes for arrangement-record actions
			if (type == ActionType::ARRANGEMENT_RECORD && thisConsequence->type == Consequence::PARAM_CHANGE) {}

			else {
				error = thisConsequence->revert(time, modelStack);
				// If an error occurs, keep swapping the order cos it's too late to stop, but don't keep calling the
				// things
			}
		}

		if (context_changed()) {
			// Keep both processed and pending history reachable. In particular,
			// do not destroy arrangement consequences using a changed song.
			Consequence** tail = &thisConsequence;
			while (*tail)
				tail = &(*tail)->next;
			*tail = type == ActionType::ARRANGEMENT_RECORD ? firstConsequence : newFirstConsequence;
			firstConsequence = thisConsequence;
			return Error::BUG;
		}

		Consequence* nextConsequence = thisConsequence->next;

		// Special case for arrangement-record. See big comment above
		if (type == ActionType::ARRANGEMENT_RECORD) {
			// Delete the old one
			thisConsequence->prepareForDestruction(
			    AFTER,
			    modelStack->song); // Have to put AFTER. See the effect this will have in
			                       // ConsequenceCDelete::prepareForDestruction()
			thisConsequence->~Consequence();
			delugeDealloc(thisConsequence);
		}

		// Or, normal case
		else {
			// Reverse the order, for next time we revert this, which will be in the other direction
			thisConsequence->next = newFirstConsequence;
			newFirstConsequence = thisConsequence;
		}

		thisConsequence = nextConsequence;
	}

	if (type != ActionType::ARRANGEMENT_RECORD) {
		firstConsequence = newFirstConsequence;
	}

	return context_changed() ? Error::BUG : error;
}

bool Action::containsConsequenceParamChange(ParamCollection* paramCollection, int32_t paramId) {
	// See if this param has already had its state snapshotted. If so, get out
	for (Consequence* thisCons = firstConsequence; thisCons; thisCons = thisCons->next) {
		if (thisCons->type == Consequence::PARAM_CHANGE) {
			ConsequenceParamChange* thisConsParamChange = (ConsequenceParamChange*)thisCons;
			if (thisConsParamChange->modelStack.paramCollection == paramCollection
			    && thisConsParamChange->modelStack.paramId == paramId) {
				return true;
			}
		}
	}
	return false;
}

bool Action::recordParamChangeIfNotAlreadySnapshotted(ModelStackWithAutoParam const* modelStack, bool stealData) {

	// If we already have a snapshot of this, we can get out.
	if (containsConsequenceParamChange(modelStack->paramCollection, modelStack->paramId)) {

		// Except, if we were planning to steal the data, well we'd better pretend we've just done that by deleting it
		// instead.
		if (stealData) {
			modelStack->autoParam->nodes.empty();
		}
		return true;
	}

	// If we're still here, we need to snapshot.
	return recordParamChangeDefinitely(modelStack, stealData);
}

bool Action::recordParamChangeDefinitely(ModelStackWithAutoParam const* modelStack, bool stealData) {

	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceParamChange));

	if (consMemory) {
		ConsequenceParamChange* newCons = new (consMemory) ConsequenceParamChange(modelStack, stealData);
		if (!newCons->snapshot_valid()) {
			newCons->~ConsequenceParamChange();
			delugeDealloc(newCons);
			return snapshot_failed();
		}
		addConsequence(newCons);
		return true;
	}
	return snapshot_failed();
}

bool Action::containsConsequenceNoteArrayChange(InstrumentClip* clip, int32_t noteRowId, bool moveToFrontIfFound) {
	// Callers supply a live edit target. A reused row ID is not an existing
	// snapshot of that target, even when its storage address is unchanged.
	if (!clip || clip->type != ClipType::INSTRUMENT)
		return false;
	auto* row = clip->getNoteRowFromId(noteRowId);
	if (!row || !row->undo_identity)
		return false;

	for (Consequence** prevPointer = &firstConsequence; *prevPointer; prevPointer = &(*prevPointer)->next) {
		Consequence* thisCons = *prevPointer;
		if (thisCons->type == Consequence::NOTE_ARRAY_CHANGE) {
			ConsequenceNoteArrayChange* thisNoteArrayChange = (ConsequenceNoteArrayChange*)thisCons;
			if (thisNoteArrayChange->clip == clip && thisNoteArrayChange->noteRowId == noteRowId
			    && thisNoteArrayChange->note_row_identity == row->undo_identity
			    && thisNoteArrayChange->snapshot_valid()) {
				if (moveToFrontIfFound) {
					*prevPointer = thisCons->next;

					thisCons->next = firstConsequence;
					firstConsequence = thisCons;
				}
				return true;
			}
		}
	}
	return false;
}

Error Action::recordNoteArrayChangeIfNotAlreadySnapshotted(InstrumentClip* clip, int32_t noteRowId,
                                                           NoteVector* noteVector, bool stealData,
                                                           bool moveToFrontIfAlreadySnapshotted) {
	if (containsConsequenceNoteArrayChange(clip, noteRowId, moveToFrontIfAlreadySnapshotted)) {
		return Error::NONE;
	}

	// If we're still here, we need to snapshot.
	return recordNoteArrayChangeDefinitely(clip, noteRowId, noteVector, stealData);
}

Error Action::recordNoteArrayChangeDefinitely(InstrumentClip* clip, int32_t noteRowId, NoteVector* noteVector,
                                              bool stealData) {
	if (!currentSong || !clip || !noteVector || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	auto* row = clip->getNoteRowFromId(noteRowId);
	if (!row || !row->undo_identity)
		return Error::BUG;
	const int32_t original_count = noteVector->getNumElements();
	Note* const original_first = original_count ? noteVector->getElement(0) : nullptr;
	NoteRow* const original_row = row;
	const auto row_identity = row->undo_identity;
	Song* const song = currentSong;
	using namespace deluge::gui::ui_session;
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteArrayChange));

	// Allocation can service callbacks. Do not access the old clip or source
	// vector if the song or structure changed while obtaining storage.
	if (currentSong != song || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision) {
		if (consMemory)
			delugeDealloc(consMemory);
		return Error::BUG;
	}
	row = clip->getNoteRowFromId(noteRowId);
	if (!row || row != original_row || row->undo_identity != row_identity) {
		if (consMemory)
			delugeDealloc(consMemory);
		return Error::BUG;
	}

	// Callers may retain indices and note pointers while recording. Refuse to
	// continue if allocation changed the source vector's shape or storage.
	if (noteVector->getNumElements() != original_count
	    || (original_count && noteVector->getElement(0) != original_first)) {
		if (consMemory)
			delugeDealloc(consMemory);
		return Error::BUG;
	}

	if (!consMemory)
		return Error::INSUFFICIENT_RAM;

	ConsequenceNoteArrayChange* newCons =
	    new (consMemory) ConsequenceNoteArrayChange(clip, noteRowId, noteVector, stealData);
	// Cloning the note vector can allocate as well. Keep the new consequence
	// private until the captured target has survived that second boundary.
	bool invalidated = currentSong != song
	                   || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	                   || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision;
	if (!invalidated) {
		row = clip->getNoteRowFromId(noteRowId);
		invalidated = !row || row != original_row || row->undo_identity != row_identity;
	}
	if (!invalidated && !stealData) {
		invalidated = noteVector->getNumElements() != original_count
		              || (original_count && noteVector->getElement(0) != original_first);
	}
	if (invalidated || !newCons->snapshot_valid()) {
		newCons->~ConsequenceNoteArrayChange();
		delugeDealloc(newCons);
		return invalidated ? Error::BUG : Error::INSUFFICIENT_RAM;
	}
	addConsequence(newCons);

	return Error::NONE;
}

Error Action::recordNoteExistenceChange(InstrumentClip* clip, int32_t noteRowId, Note* note, ExistenceChangeType type,
                                        Note** recorded_note) {

	if (recorded_note)
		*recorded_note = nullptr;
	if (!currentSong || !clip || !note || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	auto* row = clip->getNoteRowFromId(noteRowId);
	if (!row || !row->undo_identity)
		return Error::BUG;
	NoteRow* const original_row = row;
	const auto identity = row->undo_identity;
	Note saved_note = *note;
	Song* const song = currentSong;
	using namespace deluge::gui::ui_session;
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();

	if (containsConsequenceNoteArrayChange(clip, noteRowId)) {
		if (recorded_note)
			*recorded_note = note;
		return Error::NONE;
	}

	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteExistence));

	// Allocation may yield even when it fails. Check context before reporting a
	// recoverable memory error, so callers cannot recover against an invalid row.
	if (currentSong != song || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision) {
		if (consMemory)
			delugeDealloc(consMemory);
		return Error::BUG;
	}
	row = clip->getNoteRowFromId(noteRowId);
	// Callers retain their NoteRow this pointer across recording. Identity alone
	// is insufficient: relocation preserves identity but invalidates that pointer.
	if (!row || row != original_row || row->undo_identity != identity) {
		if (consMemory)
			delugeDealloc(consMemory);
		return Error::BUG;
	}
	Note* inserted = nullptr;
	int32_t index = -1;
	if (type == ExistenceChangeType::CREATE || recorded_note) {
		// Reacquire a requested target after allocation: both its address and
		// index may have changed while callers were recording history.
		index = row->notes.search(saved_note.pos, GREATER_OR_EQUAL);
		inserted = row->notes.getElement(index);
		if (!inserted || inserted->pos != saved_note.pos || inserted->length != saved_note.length
		    || inserted->velocity != saved_note.velocity || inserted->lift != saved_note.lift
		    || inserted->probability != saved_note.probability || inserted->iterance != saved_note.iterance
		    || inserted->fill != saved_note.fill) {
			if (consMemory)
				delugeDealloc(consMemory);
			return Error::BUG;
		}
	}
	if (!consMemory) {
		if (inserted && type == ExistenceChangeType::CREATE)
			row->notes.deleteAtIndex(index);
		return Error::INSUFFICIENT_RAM;
	}
	ConsequenceNoteExistence* newConsequence =
	    new (consMemory) ConsequenceNoteExistence(clip, noteRowId, &saved_note, type);
	addConsequence(newConsequence);
	if (recorded_note)
		*recorded_note = inserted;
	return Error::NONE;
}

bool Action::recordClipInstanceExistenceChange(Output* output, ClipInstance* clipInstance, ExistenceChangeType type) {

	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipInstanceExistence));

	if (consMemory) {
		ConsequenceClipInstanceExistence* newConsequence =
		    new (consMemory) ConsequenceClipInstanceExistence(output, clipInstance, type);
		addConsequence(newConsequence);
		return true;
	}
	return false;
}

bool Action::recordClipLengthChange(Clip* clip, int32_t oldLength) {
	if (!currentSong || !clip || oldLength <= 0 || clip->loopLength <= 0)
		return false;
	const uint64_t retained_identity = action_identity;
	if (!retained_identity)
		return false;
	Song* const target_song = currentSong;
	const bool registered_clip = target_song->contains_clip_for_undo(clip);
	const bool retained_in_history = actionLogger.firstAction[BEFORE] == this;
	auto* const target_output = clip->output;
	const auto target_type = clip->type;
	const int32_t target_length = clip->loopLength;
	using namespace deluge::gui::ui_session;
	const auto owner = current();
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	auto already_recorded = [&] {
		for (auto* candidate = firstConsequence; candidate; candidate = candidate->next) {
			if (candidate->type == Consequence::CLIP_LENGTH
			    && static_cast<ConsequenceClipLength*>(candidate)->clip == clip)
				return true;
		}
		return false;
	};
	if (already_recorded())
		return true;
	void* consequence_memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipLength));
	// Allocation can service callbacks. Validate external ownership before
	// accessing retained model objects or this action again.
	if (currentSong != target_song || current() != owner
	    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
	    || (retained_in_history && actionLogger.firstAction[BEFORE] != this) || action_identity != retained_identity
	    || (registered_clip && !target_song->contains_clip_for_undo(clip)) || clip->output != target_output
	    || clip->type != target_type || clip->loopLength != target_length) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return false;
	}
	if (!consequence_memory)
		return false;
	// A nested edit may have recorded the same target while allocating.
	if (already_recorded()) {
		delugeDealloc(consequence_memory);
		return true;
	}
	addConsequence(new (consequence_memory) ConsequenceClipLength(clip, oldLength));
	return true;
}

bool Action::recordClipExistenceChange(Song* song, ClipArray* clipArray, Clip* clip, ExistenceChangeType type) {
	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipExistence));
	if (!consMemory) {
		return false;
	}

	ConsequenceClipExistence* consequence = new (consMemory) ConsequenceClipExistence(clip, clipArray, type);
	if (type == ExistenceChangeType::DELETE) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, song);

		// Only record a deletion that actually succeeded. Current deletion
		// failures must leave the clip owned by its song.
		if (consequence->revert(AFTER, modelStack) != Error::NONE) {
			consequence->~ConsequenceClipExistence();
			delugeDealloc(consequence);
			return false;
		}
	}
	addConsequence(consequence);

	// For undoing looping stuff, this helps:
	xScrollClip[BEFORE] = 0;
	xScrollClip[AFTER] = 0;

	return true;
}

// Call this *before* you change the Sample or its filePath
void Action::recordAudioClipSampleChange(AudioClip* clip) {
	// for some unknown reason this doesn't work on live looping?
	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceAudioClipSetSample));
	if (consMemory) {
		ConsequenceAudioClipSetSample* cons = new (consMemory) ConsequenceAudioClipSetSample(clip);
		addConsequence(cons);
	}
}

void Action::updateYScrollClipViewAfter(InstrumentClip* clip) {
	if (!numClipStates) {
		return;
	}

	if (numClipStates
	    != currentSong->sessionClips.getNumElements() + currentSong->arrangementOnlyClips.getNumElements()) {
		numClipStates = 0;
		delugeDealloc(clipStates);
		clipStates = nullptr;
		D_PRINTLN("discarded clip states");
		return;
	}

	// NOTE: i ranges over over all clips, not just instrument clips!
	int32_t i = 0;
	for (Clip* thisClip : AllClips::everywhere(currentSong)) {
		if (thisClip->type == ClipType::INSTRUMENT) {

			if (!clip || thisClip == clip) {
				clipStates[i].yScrollSessionView[AFTER] = ((InstrumentClip*)thisClip)->y_scroll_for_session();
				break;
			}
		}

		i++;
	}
}
