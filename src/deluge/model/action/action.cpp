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
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence.h"
#include "model/consequence/consequence_arranger_params_time_inserted.h"
#include "model/consequence/consequence_audio_clip_set_sample.h"
#include "model/consequence/consequence_clip_existence.h"
#include "model/consequence/consequence_clip_instance_change.h"
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
		if (stealData && modelStack->autoParam) {
			modelStack->autoParam->nodes.empty();
		}
		return true;
	}

	// If we're still here, we need to snapshot.
	return record_param_change(modelStack, stealData, true);
}

bool Action::recordParamChangeDefinitely(ModelStackWithAutoParam const* modelStack, bool stealData) {
	return record_param_change(modelStack, stealData, false);
}

bool Action::record_param_change(ModelStackWithAutoParam const* model_stack, bool steal_data, bool only_if_missing) {
	if (!model_stack || !action_identity) {
		return false;
	}
	const uint64_t retained_action_identity = action_identity;
	const bool retained_in_history = actionLogger.firstAction[BEFORE] == this;
	const auto* song = currentSong;
	using namespace deluge::gui::ui_session;
	const auto owner = current();
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	auto context_invalidated = [&] {
		return currentSong != song || current() != owner
		       || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
		       || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
		       || (retained_in_history && actionLogger.firstAction[BEFORE] != this)
		       || action_identity != retained_action_identity;
	};
	void* consequence_memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceParamChange));
	if (context_invalidated()) {
		if (consequence_memory) {
			delugeDealloc(consequence_memory);
		}
		return false;
	}
	// Allocation can run a nested edit which publishes this parameter's snapshot.
	if (only_if_missing && containsConsequenceParamChange(model_stack->paramCollection, model_stack->paramId)) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		if (steal_data && model_stack->autoParam)
			model_stack->autoParam->nodes.empty();
		return true;
	}
	if (!consequence_memory) {
		return snapshot_failed();
	}
	ConsequenceParamChange* new_consequence = new (consequence_memory) ConsequenceParamChange(model_stack, steal_data);
	bool invalidated = context_invalidated();
	// Cloning can also yield. Keep the first published snapshot even if this
	// redundant clone failed; the nested snapshot already covers the edit.
	const bool already_recorded = !invalidated && only_if_missing
	                              && containsConsequenceParamChange(model_stack->paramCollection, model_stack->paramId);
	if (invalidated || already_recorded || !new_consequence->snapshot_valid()) {
		new_consequence->~ConsequenceParamChange();
		delugeDealloc(new_consequence);
		return invalidated ? false : (already_recorded ? true : snapshot_failed());
	}
	addConsequence(new_consequence);
	return true;
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
	return record_note_array_change(clip, noteRowId, noteVector, stealData, true, moveToFrontIfAlreadySnapshotted);
}

Error Action::recordNoteArrayChangeDefinitely(InstrumentClip* clip, int32_t noteRowId, NoteVector* noteVector,
                                              bool stealData) {
	return record_note_array_change(clip, noteRowId, noteVector, stealData, false, false);
}

Error Action::record_note_array_change(InstrumentClip* clip, int32_t note_row_id, NoteVector* note_vector,
                                       bool steal_data, bool only_if_missing, bool move_to_front) {
	if (!currentSong || !clip || !note_vector || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	auto* row = clip->getNoteRowFromId(note_row_id);
	if (!row || !row->undo_identity)
		return Error::BUG;
	const uint64_t retained_action_identity = action_identity;
	if (!retained_action_identity) {
		return Error::BUG;
	}
	const bool retained_in_history = actionLogger.firstAction[BEFORE] == this;
	const int32_t original_count = note_vector->getNumElements();
	Note* const original_first = original_count ? note_vector->getElement(0) : nullptr;
	NoteRow* const original_row = row;
	const auto row_identity = row->undo_identity;
	Song* const song = currentSong;
	using namespace deluge::gui::ui_session;
	const auto owner = current();
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	auto context_invalidated = [&] {
		// External history ownership must be checked before accessing the retained action.
		return currentSong != song || current() != owner
		       || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
		       || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
		       || (retained_in_history && actionLogger.firstAction[BEFORE] != this)
		       || action_identity != retained_action_identity;
	};
	void* consequence_memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteArrayChange));

	// Allocation can service callbacks. Do not access the old clip or source
	// vector if the song or structure changed while obtaining storage.
	if (context_invalidated()) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return Error::BUG;
	}
	row = clip->getNoteRowFromId(note_row_id);
	if (!row || row != original_row || row->undo_identity != row_identity) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return Error::BUG;
	}

	// Callers may retain indices and note pointers while recording. Refuse to
	// continue if allocation changed the source vector's shape or storage.
	if (note_vector->getNumElements() != original_count
	    || (original_count && note_vector->getElement(0) != original_first)) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return Error::BUG;
	}

	if (only_if_missing && containsConsequenceNoteArrayChange(clip, note_row_id, move_to_front)) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return Error::NONE;
	}

	if (!consequence_memory)
		return Error::INSUFFICIENT_RAM;

	ConsequenceNoteArrayChange* new_consequence =
	    new (consequence_memory) ConsequenceNoteArrayChange(clip, note_row_id, note_vector, steal_data);
	// Cloning the note vector can allocate as well. Keep the new consequence
	// private until the captured target has survived that second boundary.
	bool invalidated = context_invalidated();
	if (!invalidated) {
		row = clip->getNoteRowFromId(note_row_id);
		invalidated = !row || row != original_row || row->undo_identity != row_identity;
	}
	if (!invalidated && !steal_data) {
		invalidated = note_vector->getNumElements() != original_count
		              || (original_count && note_vector->getElement(0) != original_first);
	}
	const bool already_recorded =
	    !invalidated && only_if_missing && containsConsequenceNoteArrayChange(clip, note_row_id, move_to_front);
	if (invalidated || already_recorded || !new_consequence->snapshot_valid()) {
		new_consequence->~ConsequenceNoteArrayChange();
		delugeDealloc(new_consequence);
		return invalidated ? Error::BUG : (already_recorded ? Error::NONE : Error::INSUFFICIENT_RAM);
	}
	addConsequence(new_consequence);

	return Error::NONE;
}

void* Action::allocate_note_existence_memory() {
	return GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteExistence));
}

Error Action::recordNoteExistenceChange(InstrumentClip* clip, int32_t noteRowId, Note* note, ExistenceChangeType type,
                                        Note** recorded_note, void** prepared_memory) {

	if (recorded_note)
		*recorded_note = nullptr;
	if (!currentSong || !clip || !note || clip->type != ClipType::INSTRUMENT)
		return Error::BUG;
	auto* row = clip->getNoteRowFromId(noteRowId);
	if (!row || !row->undo_identity)
		return Error::BUG;
	const uint64_t retained_action_identity = action_identity;
	if (!retained_action_identity) {
		return Error::BUG;
	}
	const bool retained_in_history = actionLogger.firstAction[BEFORE] == this;
	NoteRow* const original_row = row;
	const auto identity = row->undo_identity;
	Note saved_note = *note;
	Song* const song = currentSong;
	using namespace deluge::gui::ui_session;
	const auto owner = current();
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();

	if (containsConsequenceNoteArrayChange(clip, noteRowId)) {
		if (recorded_note)
			*recorded_note = note;
		return Error::NONE;
	}

	void* consMemory = prepared_memory ? *prepared_memory : allocate_note_existence_memory();
	if (prepared_memory) {
		*prepared_memory = nullptr;
	}

	// Allocation may yield even when it fails. Check context before reporting a
	// recoverable memory error, so callers cannot recover against an invalid row.
	if (currentSong != song || current() != owner
	    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
	    || (retained_in_history && actionLogger.firstAction[BEFORE] != this)
	    || action_identity != retained_action_identity) {
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
	Song* const song = currentSong;
	if (!song || !output || !clipInstance || !action_identity || !song->owns_output_for_undo(output))
		return false;
	const uint64_t retained_identity = action_identity;
	const bool retained_in_history = actionLogger.firstAction[BEFORE] == this;
	const int32_t target_pos = clipInstance->pos;
	const int32_t target_length = clipInstance->length;
	Clip* const target_clip = clipInstance->clip;
	const bool registered_clip = target_clip && song->contains_clip_for_undo(target_clip);
	using namespace deluge::gui::ui_session;
	const auto owner = current();
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	void* consequence_memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipInstanceExistence));
	if (currentSong != song || current() != owner
	    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
	    || (retained_in_history && actionLogger.firstAction[BEFORE] != this) || action_identity != retained_identity
	    || !song->owns_output_for_undo(output) || (registered_clip && !song->contains_clip_for_undo(target_clip))) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return false;
	}
	// Reacquire through the live output before reading instance storage. Callers
	// may still hold the old pointer, so relocation must also reject recording.
	auto* live_instance = output->clipInstances.getElement(output->clipInstances.search(target_pos, GREATER_OR_EQUAL));
	if (!live_instance || live_instance != clipInstance || live_instance->pos != target_pos
	    || live_instance->length != target_length || live_instance->clip != target_clip) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return false;
	}
	if (!consequence_memory)
		return snapshot_failed();
	addConsequence(new (consequence_memory) ConsequenceClipInstanceExistence(output, live_instance, type));
	return true;
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
	// A nested edit may have recorded the same target even if our allocation
	// failed. Its valid snapshot already covers the edit.
	if (already_recorded()) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return true;
	}
	if (!consequence_memory)
		return false;
	addConsequence(new (consequence_memory) ConsequenceClipLength(clip, oldLength));
	return true;
}

bool Action::recordClipExistenceChange(Song* song, ClipArray* clipArray, Clip* clip, ExistenceChangeType type) {
	if (!song || song != currentSong || !clipArray || !clip || !action_identity
	    || (clipArray != &song->sessionClips && clipArray != &song->arrangementOnlyClips))
		return false;
	const uint64_t retained_identity = action_identity;
	Consequence* const history_boundary = firstConsequence;
	const bool retained_in_history = actionLogger.firstAction[BEFORE] == this;
	Song* const active_song = currentSong;
	const bool registered_clip = song->contains_clip_for_undo(clip);
	const bool in_target_array = song->get_clip_index_for_undo(clipArray, clip) >= 0;
	if ((registered_clip || type == ExistenceChangeType::DELETE) && !in_target_array)
		return false;
	auto* const target_output = clip->output;
	const bool registered_output = song->owns_output_for_undo(target_output);
	const auto target_type = clip->type;
	using namespace deluge::gui::ui_session;
	const auto owner = current();
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceClipExistence));
	// Check external ownership before accessing retained action/model pointers.
	// In particular, do not begin deletion after allocation invalidates its target.
	if (currentSong != active_song || current() != owner
	    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
	    || (retained_in_history && actionLogger.firstAction[BEFORE] != this) || action_identity != retained_identity
	    || firstConsequence != history_boundary || (registered_output && !song->owns_output_for_undo(target_output))
	    || (registered_clip && !song->contains_clip_for_undo(clip))
	    || ((in_target_array || song->contains_clip_for_undo(clip))
	        && song->get_clip_index_for_undo(clipArray, clip) < 0)
	    || clip->output != target_output || clip->type != target_type) {
		if (consMemory)
			delugeDealloc(consMemory);
		return false;
	}
	if (!consMemory) {
		return snapshot_failed();
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

// Caller validates retained action/session identity. This path never allocates or
// services callbacks: validate the entire batch before changing any model state.
bool Action::rollback_instance_batch(Song* song, Consequence* boundary, int32_t count, int32_t displacement,
                                     bool allow_attached_clips) {
	if (!song || song != currentSong || count < 0)
		return false;
	auto* cursor = firstConsequence;
	for (int32_t index = 0; index < count; ++index) {
		if (!cursor || cursor == boundary
		    || (cursor->type != Consequence::CLIP_INSTANCE_CHANGE
		        && cursor->type != Consequence::CLIP_INSTANCE_EXISTENCE))
			return false;
		cursor = cursor->next;
	}
	if (cursor != boundary)
		return false;
	struct instance_snapshot {
		Output* output;
		Clip* clip;
		int32_t before_pos, before_length, after_pos, after_length;
		bool deleted;
	};
	auto read_snapshot = [](Consequence* item) -> instance_snapshot {
		if (item->type == Consequence::CLIP_INSTANCE_CHANGE) {
			auto* change = static_cast<ConsequenceClipInstanceChange*>(item);
			return {change->output,
			        change->clip[BEFORE],
			        change->pos[BEFORE],
			        change->length[BEFORE],
			        change->pos[AFTER],
			        change->length[AFTER],
			        false};
		}
		auto* deletion = static_cast<ConsequenceClipInstanceExistence*>(item);
		return {deletion->output, deletion->clip, deletion->pos, deletion->length, 0, 0, true};
	};
	auto overlaps = [](int32_t a_pos, int32_t a_length, int32_t b_pos, int32_t b_length) {
		return static_cast<int64_t>(a_pos) < static_cast<int64_t>(b_pos) + b_length
		       && static_cast<int64_t>(b_pos) < static_cast<int64_t>(a_pos) + a_length;
	};
	for (auto* item = firstConsequence; item != boundary; item = item->next) {
		const auto snapshot = read_snapshot(item);
		if (snapshot.before_length <= 0 || !song->can_reference_clip_from_output(snapshot.clip, snapshot.output))
			return false;
		auto& instances = snapshot.output->clipInstances;
		if (snapshot.deleted) {
			if ((snapshot.clip && !allow_attached_clips) || displacement > 0
			    || static_cast<ConsequenceClipInstanceExistence*>(item)->type != ExistenceChangeType::DELETE)
				return false;
		}
		else {
			const bool translated = displacement != 0
			                        && static_cast<int64_t>(snapshot.after_pos) - snapshot.before_pos == displacement
			                        && snapshot.before_length == snapshot.after_length;
			const bool shortened = displacement < 0 && snapshot.before_pos == snapshot.after_pos
			                       && snapshot.after_length < snapshot.before_length;
			if ((!translated && !shortened) || snapshot.after_length <= 0
			    || static_cast<ConsequenceClipInstanceChange*>(item)->clip[AFTER] != snapshot.clip)
				return false;
			auto* target = instances.getElement(instances.search(snapshot.after_pos, GREATER_OR_EQUAL));
			if (!target || target->pos != snapshot.after_pos || target->length != snapshot.after_length
			    || target->clip != snapshot.clip)
				return false;
		}
		int32_t needed = 0;
		for (auto* peer = firstConsequence; peer != boundary; peer = peer->next) {
			const auto other = read_snapshot(peer);
			if (other.output != snapshot.output)
				continue;
			needed += other.deleted;
			if (peer != item
			    && (overlaps(snapshot.before_pos, snapshot.before_length, other.before_pos, other.before_length)
			        || (!snapshot.deleted && !other.deleted && snapshot.after_pos == other.after_pos)))
				return false;
		}
		if (!instances.has_capacity_for(needed))
			return false;
		for (int32_t index = 0; index < instances.getNumElements(); ++index) {
			auto* live = instances.getElement(index);
			bool restored_by_batch = false;
			for (auto* peer = firstConsequence; peer != boundary; peer = peer->next) {
				const auto other = read_snapshot(peer);
				if (!other.deleted && other.output == snapshot.output && other.after_pos == live->pos) {
					restored_by_batch = true;
					break;
				}
			}
			if (!restored_by_batch
			    && (live->length <= 0
			        || overlaps(snapshot.before_pos, snapshot.before_length, live->pos, live->length)))
				return false;
		}
	}
	// No allocation or callbacks after validation. Reverse the batch in history
	// order so moved instances vacate deleted instances' original positions first.
	char stack_memory[MODEL_STACK_MAX_SIZE];
	auto* stack = setupModelStackWithSong(stack_memory, song);
	while (firstConsequence != boundary) {
		auto* item = firstConsequence;
		if (item->type == Consequence::CLIP_INSTANCE_CHANGE) {
			if (item->revert(BEFORE, stack) != Error::NONE)
				return false;
		}
		else {
			const auto snapshot = read_snapshot(item);
			auto& instances = snapshot.output->clipInstances;
			const int32_t index = instances.search(snapshot.before_pos, GREATER_OR_EQUAL);
			if (instances.insert_at_index_without_allocation(index) != Error::NONE)
				return false;
			auto* instance = instances.getElement(index);
			instance->pos = snapshot.before_pos;
			instance->length = snapshot.before_length;
			instance->clip = snapshot.clip;
		}
		firstConsequence = item->next;
		item->~Consequence();
		delugeDealloc(item);
	}
	return true;
}

bool Action::rollback_empty_instance_deletions(Song* song, Consequence* boundary, int32_t count) {
	return rollback_instance_batch(song, boundary, count, 0);
}

bool Action::record_arranger_time_inserted(int32_t target_pos, int32_t target_length) {
	Song* const song = currentSong;
	if (!song || !action_identity || target_pos < 0 || target_length <= 0)
		return false;
	const uint64_t retained_identity = action_identity;
	const bool retained_in_history = actionLogger.firstAction[BEFORE] == this;
	auto* const history_boundary = firstConsequence;
	using namespace deluge::gui::ui_session;
	const auto owner = current();
	const auto local_revision = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_revision = navigation.for_owner(Id::Remote).structural_refresh.revision();
	void* consequence_memory =
	    GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceArrangerParamsTimeInserted));
	if (currentSong != song || current() != owner
	    || navigation.for_owner(Id::Local).structural_refresh.revision() != local_revision
	    || navigation.for_owner(Id::Remote).structural_refresh.revision() != remote_revision
	    || (retained_in_history && actionLogger.firstAction[BEFORE] != this) || action_identity != retained_identity
	    || firstConsequence != history_boundary || song->x_scroll_for_session()[NAVIGATION_ARRANGEMENT] != target_pos) {
		if (consequence_memory)
			delugeDealloc(consequence_memory);
		return false;
	}
	if (!consequence_memory)
		return snapshot_failed();
	addConsequence(new (consequence_memory) ConsequenceArrangerParamsTimeInserted(target_pos, target_length));
	return true;
}

// Called only after instance rollback has validated the retained action/context.
bool Action::rollback_arranger_time_inserted(Song* song, Consequence* expected_head) {
	if (!song || song != currentSong || !expected_head || firstConsequence != expected_head
	    || expected_head->type != Consequence::ARRANGER_TIME_INSERTED)
		return false;
	char stack_memory[MODEL_STACK_MAX_SIZE];
	auto* stack = setupModelStackWithSong(stack_memory, song);
	// The insertion inverse validates every parameter before changing any nodes.
	if (expected_head->revert(BEFORE, stack) != Error::NONE)
		return false;
	firstConsequence = expected_head->next;
	expected_head->~Consequence();
	delugeDealloc(expected_head);
	return true;
}
