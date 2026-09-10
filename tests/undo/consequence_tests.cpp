#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "model/consequence/consequence_clip_horizontal_shift.h"
#include "model/consequence/consequence_clip_length.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "model/consequence/consequence_note_row_length.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "undo_model.h"
#include <limits>
#include <memory>

static SimpleString StringFrom(Error error) {
	return StringFrom(static_cast<int>(error));
}

TEST_GROUP(UndoConsequences) {
	Song song;
	Output output;
	InstrumentClip target, other;
	NoteRow row, other_row;
	ModelStack stack;
	void setup() override {
		target.output = &output;
		other.output = &output;
		target.row = &row;
		other.row = &other_row;
		song.registered = {&target, &other};
		song.selected = &other;
		stack.song = &song;
		row.notes.values = {1, 2, 3};
		NoteVector::fail_clone = false;
	}
	void teardown() override {
		NoteVector::fail_clone = false;
	}
};

TEST(UndoConsequences, clip_shift_uses_retained_target_and_round_trips) {
	ConsequenceClipHorizontalShift c(&target, 12, true, false);
	CHECK_EQUAL(Error::NONE, c.revert(BEFORE, &stack));
	CHECK_EQUAL(-12, target.total_shift);
	CHECK_TRUE(target.last_automation);
	CHECK_FALSE(target.last_sequence);
	CHECK_EQUAL(0, other.shift_calls);
	CHECK_EQUAL(Error::NONE, c.revert(AFTER, &stack));
	CHECK_EQUAL(0, target.total_shift);
}

TEST(UndoConsequences, rejected_clip_shift_propagates_failure) {
	target.shift_succeeds = false;
	ConsequenceClipHorizontalShift c(&target, 12, false, true);
	CHECK_EQUAL(Error::BUG, c.revert(BEFORE, &stack));
	CHECK_EQUAL(1, target.shift_calls);
	CHECK_EQUAL(0, target.total_shift);
	CHECK_EQUAL(12, c.amount);
}

TEST(UndoConsequences, clip_shift_rejects_unrepresentable_inverse_without_dispatch) {
	ConsequenceClipHorizontalShift c(&target, std::numeric_limits<int32_t>::min(), true, true);
	for (auto time : {BEFORE, AFTER})
		CHECK_EQUAL(Error::BUG, c.revert(time, &stack));
	CHECK_EQUAL(0, target.shift_calls);
}

TEST(UndoConsequences, shift_accumulation_requires_same_target_flags_and_reversible_range) {
	ConsequenceClipHorizontalShift c(&target, 10, true, false);
	CHECK_TRUE(c.can_accumulate(&target, -10, true, false));
	CHECK_FALSE(c.can_accumulate(&other, 1, true, false));
	CHECK_FALSE(c.can_accumulate(&target, 1, false, false));
	CHECK_FALSE(c.can_accumulate(&target, 1, true, true));
	c.amount = std::numeric_limits<int32_t>::max();
	CHECK_FALSE(c.can_accumulate(&target, 1, true, false));
	CHECK_TRUE(c.can_accumulate(&target, -1, true, false));
	c.amount = -std::numeric_limits<int32_t>::max();
	CHECK_FALSE(c.can_accumulate(&target, -1, true, false));
}

TEST(UndoConsequences, all_retained_targets_reject_missing_song_or_unregistered_clip) {
	NoteVector snapshot;
	snapshot.values = {9};
	ConsequenceClipHorizontalShift shift(&target, 1, true, true);
	ConsequenceClipLength length(&target, 48);
	ConsequenceNoteArrayChange notes(&target, 7, &snapshot, false);
	ConsequenceNoteRowHorizontalShift row_shift(&target, 7, 1, true, true);
	ConsequenceNoteRowLength row_length(&target, 7, 48);
	ConsequenceNoteRowMute mute(&target, 7);
	target.row_lookups = 0; // Only reversion must avoid looking up an unowned clip.
	for (Consequence* c : std::initializer_list<Consequence*>{static_cast<Consequence*>(&shift), &length, &notes,
	                                                          &row_shift, &row_length, &mute}) {
		CHECK_EQUAL(Error::BUG, c->revert(BEFORE, nullptr));
		stack.song = nullptr;
		CHECK_EQUAL(Error::BUG, c->revert(BEFORE, &stack));
		stack.song = &song;
		song.registered = {&other};
		CHECK_EQUAL(Error::BUG, c->revert(BEFORE, &stack));
	}
	CHECK_EQUAL(0, target.row_lookups);
	CHECK_EQUAL(0, target.shift_calls);
	CHECK_EQUAL(0, song.length_calls);
	CHECK_EQUAL(0, row.mute_calls);
	CHECK_EQUAL(0, row.length_calls);
	CHECK_EQUAL(1, row.notes.values.front());
}

TEST(UndoConsequences, destroyed_clip_is_rejected_before_dereference) {
	auto* dead = new InstrumentClip;
	ConsequenceClipHorizontalShift shift(dead, 1, true, true);
	ConsequenceClipLength length(dead, 48);
	ConsequenceNoteRowHorizontalShift row_shift(dead, 7, 1, true, true);
	ConsequenceNoteRowLength row_length(dead, 7, 48);
	ConsequenceNoteRowMute mute(dead, 7);
	NoteVector snapshot;
	ConsequenceNoteArrayChange notes(dead, 7, &snapshot, false);
	delete dead;
	for (Consequence* c : std::initializer_list<Consequence*>{static_cast<Consequence*>(&shift), &length, &row_shift,
	                                                          &row_length, &mute, &notes})
		CHECK_EQUAL(Error::BUG, c->revert(BEFORE, &stack));
}

TEST(UndoConsequences, row_targets_reject_wrong_clip_type_before_row_lookup) {
	target.type = ClipType::AUDIO;
	NoteVector snapshot;
	ConsequenceNoteArrayChange notes(&target, 7, &snapshot, false);
	ConsequenceNoteRowHorizontalShift shift(&target, 7, 1, true, true);
	ConsequenceNoteRowLength length(&target, 7, 48);
	ConsequenceNoteRowMute mute(&target, 7);
	for (Consequence* c :
	     std::initializer_list<Consequence*>{static_cast<Consequence*>(&notes), &shift, &length, &mute})
		CHECK_EQUAL(Error::BUG, c->revert(BEFORE, &stack));
	CHECK_EQUAL(0, target.row_lookups);
}

TEST(UndoConsequences, missing_row_rejects_all_row_edits_without_touching_other_selection) {
	target.row = nullptr;
	NoteVector snapshot;
	ConsequenceNoteArrayChange notes(&target, 7, &snapshot, false);
	ConsequenceNoteRowHorizontalShift shift(&target, 7, 1, true, true);
	ConsequenceNoteRowLength length(&target, 7, 48);
	ConsequenceNoteRowMute mute(&target, 7);
	for (Consequence* c :
	     std::initializer_list<Consequence*>{static_cast<Consequence*>(&notes), &shift, &length, &mute})
		CHECK_EQUAL(Error::BUG, c->revert(BEFORE, &stack));
	CHECK_EQUAL(0, other.row_lookups);
	CHECK_EQUAL(0, other.row_shift_calls);
	CHECK_EQUAL(0, other_row.length_calls);
	CHECK_EQUAL(0, other_row.mute_calls);
}

TEST(UndoConsequences, row_shift_and_length_reject_invalid_lengths_before_mutation) {
	ConsequenceNoteRowHorizontalShift shift(&target, 7, 12, true, false);
	ConsequenceNoteRowLength length(&target, 7, 48);
	for (int parent : {0, -1}) {
		target.loopLength = parent;
		CHECK_EQUAL(Error::BUG, shift.revert(BEFORE, &stack));
		CHECK_EQUAL(Error::BUG, length.revert(BEFORE, &stack));
	}
	target.loopLength = 96;
	row.loopLengthIfIndependent = -1;
	CHECK_EQUAL(Error::BUG, shift.revert(BEFORE, &stack));
	CHECK_EQUAL(Error::BUG, length.revert(BEFORE, &stack));
	row.loopLengthIfIndependent = 0;
	for (int invalid : {0, -1}) {
		length.backedUpLength = invalid;
		CHECK_EQUAL(Error::BUG, length.revert(BEFORE, &stack));
	}
	CHECK_EQUAL(0, target.row_shift_calls);
	CHECK_EQUAL(0, row.length_calls);
}

TEST(UndoConsequences, row_shift_retains_clip_flags_and_round_trips) {
	ConsequenceNoteRowHorizontalShift c(&target, 7, 12, false, true);
	CHECK_EQUAL(Error::NONE, c.revert(BEFORE, &stack));
	CHECK_EQUAL(-12, target.row_shift);
	CHECK_FALSE(target.last_automation);
	CHECK_TRUE(target.last_sequence);
	CHECK_EQUAL(Error::NONE, c.revert(AFTER, &stack));
	CHECK_EQUAL(0, target.row_shift);
	CHECK_EQUAL(0, other.row_shift_calls);
	c.amount = std::numeric_limits<int32_t>::min();
	CHECK_EQUAL(Error::BUG, c.revert(BEFORE, &stack));
	CHECK_EQUAL(2, target.row_shift_calls);
}

TEST(UndoConsequences, row_length_restores_effective_inherited_length_on_redo) {
	ConsequenceNoteRowLength c(&target, 7, 48);
	CHECK_EQUAL(Error::NONE, c.revert(BEFORE, &stack));
	CHECK_EQUAL(48, row.loopLengthIfIndependent);
	CHECK_EQUAL(96, c.backedUpLength);
	CHECK_EQUAL(Error::NONE, c.revert(AFTER, &stack));
	CHECK_EQUAL(96, row.loopLengthIfIndependent);
	CHECK_EQUAL(48, c.backedUpLength);
	CHECK_EQUAL(0, other_row.length_calls);
}

TEST(UndoConsequences, mute_round_trips_only_retained_row) {
	ConsequenceNoteRowMute c(&target, 7);
	CHECK_EQUAL(Error::NONE, c.revert(BEFORE, &stack));
	CHECK_TRUE(row.muted);
	CHECK_EQUAL(Error::NONE, c.revert(AFTER, &stack));
	CHECK_FALSE(row.muted);
	CHECK_EQUAL(0, other_row.mute_calls);
}

TEST(UndoConsequences, note_array_swaps_saved_state_and_round_trips) {
	NoteVector saved;
	saved.values = {4, 5};
	ConsequenceNoteArrayChange c(&target, 7, &saved, false);
	CHECK_TRUE(c.snapshot_valid());
	CHECK_EQUAL(Error::NONE, c.revert(BEFORE, &stack));
	CHECK_EQUAL(2, row.notes.values.size());
	CHECK_EQUAL(4, row.notes.values.front());
	CHECK_EQUAL(Error::NONE, c.revert(AFTER, &stack));
	CHECK_EQUAL(3, row.notes.values.size());
	CHECK_EQUAL(1, row.notes.values.front());
}

TEST(UndoConsequences, failed_snapshot_cannot_replace_live_notes_with_empty_storage) {
	NoteVector saved;
	saved.values = {4, 5};
	NoteVector::fail_clone = true;
	ConsequenceNoteArrayChange c(&target, 7, &saved, false);
	CHECK_FALSE(c.snapshot_valid());
	CHECK_EQUAL(Error::BUG, c.revert(BEFORE, &stack));
	CHECK_EQUAL(3, row.notes.values.size());
	CHECK_EQUAL(1, row.notes.values.front());
}

TEST(UndoConsequences, clip_length_markers_round_trip_and_precede_length_change) {
	AudioClip audio;
	audio.output = &output;
	song.registered.push_back(&audio);
	for (auto marker : {ConsequenceClipLength::SampleMarker::START, ConsequenceClipLength::SampleMarker::END}) {
		ConsequenceClipLength c(&audio, 48);
		c.sample_marker = marker;
		c.markerValueToRevertTo = 30;
		auto& value = marker == ConsequenceClipLength::SampleMarker::START ? audio.sampleHolder.startPos
		                                                                   : audio.sampleHolder.endPos;
		const auto original = value;
		CHECK_EQUAL(Error::NONE, c.revert(BEFORE, &stack));
		CHECK_EQUAL(30, value);
		CHECK_EQUAL(48, audio.loopLength);
		if (marker == ConsequenceClipLength::SampleMarker::START)
			CHECK_EQUAL(30, song.marker_observed_at_length_change);
		CHECK_EQUAL(Error::NONE, c.revert(AFTER, &stack));
		CHECK_EQUAL(original, value);
		CHECK_EQUAL(96, audio.loopLength);
	}
}

TEST(UndoConsequences, clip_length_rejects_wrong_type_or_invalid_marker_before_mutation) {
	ConsequenceClipLength wrong(&target, 48);
	wrong.sample_marker = ConsequenceClipLength::SampleMarker::START;
	wrong.markerValueToRevertTo = 50;
	CHECK_EQUAL(Error::BUG, wrong.revert(BEFORE, &stack));
	AudioClip audio;
	audio.output = &output;
	song.registered.push_back(&audio);
	ConsequenceClipLength invalid(&audio, 48);
	invalid.sample_marker = static_cast<ConsequenceClipLength::SampleMarker>(255);
	invalid.markerValueToRevertTo = 50;
	CHECK_EQUAL(Error::BUG, invalid.revert(BEFORE, &stack));
	CHECK_EQUAL(0, song.length_calls);
	CHECK_EQUAL(96, audio.loopLength);
	CHECK_EQUAL(10, audio.sampleHolder.startPos);
	CHECK_EQUAL(100, audio.sampleHolder.endPos);
}

TEST(UndoConsequences, clip_length_rejects_nonpositive_saved_length_before_marker_swap) {
	AudioClip audio;
	audio.output = &output;
	song.registered.push_back(&audio);
	for (int length : {0, -1}) {
		ConsequenceClipLength c(&audio, length);
		c.sample_marker = ConsequenceClipLength::SampleMarker::START;
		c.markerValueToRevertTo = 50;
		CHECK_EQUAL(Error::BUG, c.revert(BEFORE, &stack));
	}
	CHECK_EQUAL(0, song.length_calls);
	CHECK_EQUAL(10, audio.sampleHolder.startPos);
}

TEST(UndoConsequences, clip_length_without_marker_round_trips_only_retained_clip) {
	ConsequenceClipLength c(&target, 48);
	CHECK_EQUAL(Error::NONE, c.revert(BEFORE, &stack));
	CHECK_EQUAL(48, target.loopLength);
	CHECK_EQUAL(96, other.loopLength);
	CHECK_EQUAL(Error::NONE, c.revert(AFTER, &stack));
	CHECK_EQUAL(96, target.loopLength);
	CHECK_EQUAL(48, c.lengthToRevertTo);
}

TEST(UndoConsequences, invalid_current_length_does_not_create_invalid_redo_or_swap_marker) {
	AudioClip audio;
	audio.output = &output;
	audio.loopLength = 0;
	song.registered.push_back(&audio);
	ConsequenceClipLength c(&audio, 48);
	c.sample_marker = ConsequenceClipLength::SampleMarker::START;
	c.markerValueToRevertTo = 50;
	CHECK_EQUAL(Error::BUG, c.revert(BEFORE, &stack));
	CHECK_EQUAL(0, song.length_calls);
	CHECK_EQUAL(10, audio.sampleHolder.startPos);
	CHECK_EQUAL(48, c.lengthToRevertTo);
	CHECK_EQUAL(50, c.markerValueToRevertTo);
}

TEST(UndoConsequences, clip_length_propagates_reported_failure_even_when_length_matches) {
	ConsequenceClipLength consequence(&target, 48);
	song.length_change_succeeds = false;
	CHECK_EQUAL(Error::BUG, consequence.revert(BEFORE, &stack));
	CHECK_EQUAL(48, target.loopLength);
	CHECK_EQUAL(48, consequence.lengthToRevertTo);
}

TEST(UndoConsequences, clip_length_failure_preserves_saved_length_and_marker) {
	AudioClip audio;
	audio.output = &output;
	song.registered.push_back(&audio);
	ConsequenceClipLength consequence(&audio, 48);
	consequence.sample_marker = ConsequenceClipLength::SampleMarker::START;
	consequence.markerValueToRevertTo = 50;
	song.on_set_clip_length = [&] { audio.loopLength = 96; };
	CHECK_EQUAL(Error::BUG, consequence.revert(BEFORE, &stack));
	CHECK_EQUAL(48, consequence.lengthToRevertTo);
	CHECK_EQUAL(50, consequence.markerValueToRevertTo);
}

TEST(UndoConsequences, clip_length_callback_removal_is_rejected_before_target_access) {
	auto* audio = new AudioClip;
	audio->output = &output;
	song.registered.push_back(audio);
	ConsequenceClipLength consequence(audio, 48);
	consequence.sample_marker = ConsequenceClipLength::SampleMarker::START;
	consequence.markerValueToRevertTo = 50;
	song.on_set_clip_length = [&] {
		song.registered.pop_back();
		delete audio;
	};
	CHECK_EQUAL(Error::BUG, consequence.revert(BEFORE, &stack));
	CHECK_EQUAL(48, consequence.lengthToRevertTo);
}

TEST(UndoConsequences, clip_length_callback_redirected_song_is_rejected) {
	Song replacement;
	ConsequenceClipLength consequence(&target, 48);
	song.on_set_clip_length = [&] { stack.song = &replacement; };
	CHECK_EQUAL(Error::BUG, consequence.revert(BEFORE, &stack));
	CHECK_EQUAL(48, consequence.lengthToRevertTo);
}

TEST(UndoConsequences, clip_length_rejects_callback_changes_to_requested_sample_marker) {
	for (auto marker : {ConsequenceClipLength::SampleMarker::START, ConsequenceClipLength::SampleMarker::END}) {
		AudioClip audio;
		audio.output = &output;
		song.registered.push_back(&audio);
		ConsequenceClipLength consequence(&audio, 48);
		consequence.sample_marker = marker;
		consequence.markerValueToRevertTo = 50;
		song.on_set_clip_length = [&] {
			auto& value = marker == ConsequenceClipLength::SampleMarker::START ? audio.sampleHolder.startPos
			                                                                   : audio.sampleHolder.endPos;
			value = 60;
		};
		CHECK_EQUAL(Error::BUG, consequence.revert(BEFORE, &stack));
		CHECK_EQUAL(48, consequence.lengthToRevertTo);
		CHECK_EQUAL(50, consequence.markerValueToRevertTo);
		song.on_set_clip_length = {};
		song.registered.pop_back();
	}
}

TEST(UndoConsequences, marker_undo_rejects_callback_changes_to_opposite_marker) {
	for (auto marker : {ConsequenceClipLength::SampleMarker::START, ConsequenceClipLength::SampleMarker::END}) {
		for (auto time : {BEFORE, AFTER}) {
			AudioClip audio;
			audio.output = &output;
			song.registered.push_back(&audio);
			ConsequenceClipLength consequence(&audio, 48);
			consequence.sample_marker = marker;
			consequence.markerValueToRevertTo = 50;
			song.on_set_clip_length = [&] {
				auto& opposite = marker == ConsequenceClipLength::SampleMarker::START ? audio.sampleHolder.endPos
				                                                                      : audio.sampleHolder.startPos;
				++opposite;
			};
			CHECK_EQUAL(Error::BUG, consequence.revert(time, &stack));
			CHECK_EQUAL(48, consequence.lengthToRevertTo);
			CHECK_EQUAL(50, consequence.markerValueToRevertTo);
			song.on_set_clip_length = {};
			song.registered.pop_back();
		}
	}
}

TEST(UndoConsequences, clip_length_rejects_callback_context_changes_without_updating_redo) {
	Song replacement_song;
	Output replacement_output;
	Song* const original_song = currentSong;
	auto* const original_output = target.output;
	for (int change = 0; change < 5; ++change) {
		ConsequenceClipLength consequence(&target, 48);
		song.on_set_clip_length = [&] {
			switch (change) {
			case 0:
				currentSong = &replacement_song;
				break;
			case 1:
				target.output = &replacement_output;
				break;
			case 2:
				target.type = ClipType::AUDIO;
				break;
			default:
				deluge::gui::ui_session::navigation
				    .for_owner(change == 3 ? deluge::gui::ui_session::Id::Local : deluge::gui::ui_session::Id::Remote)
				    .structural_refresh.request();
			}
		};
		const auto result = consequence.revert(BEFORE, &stack);
		currentSong = original_song;
		target.output = original_output;
		target.type = ClipType::INSTRUMENT;
		target.loopLength = 96;
		song.on_set_clip_length = {};
		CHECK_EQUAL(Error::BUG, result);
		CHECK_EQUAL(48, consequence.lengthToRevertTo);
	}
}

TEST(UndoConsequences, marker_undo_rejects_sample_or_direction_changes_during_resize) {
	Sample replacement_sample;
	for (auto marker : {ConsequenceClipLength::SampleMarker::START, ConsequenceClipLength::SampleMarker::END}) {
		for (bool change_sample : {false, true}) {
			AudioClip audio;
			audio.output = &output;
			song.registered.push_back(&audio);
			ConsequenceClipLength consequence(&audio, 48);
			consequence.sample_marker = marker;
			consequence.markerValueToRevertTo = 50;
			song.on_set_clip_length = [&] {
				if (change_sample)
					audio.sampleHolder.audioFile = &replacement_sample;
				else
					audio.sampleControls.reversed = true;
			};
			CHECK_EQUAL(Error::BUG, consequence.revert(BEFORE, &stack));
			CHECK_EQUAL(48, consequence.lengthToRevertTo);
			CHECK_EQUAL(50, consequence.markerValueToRevertTo);
			song.on_set_clip_length = {};
			song.registered.pop_back();
		}
	}
}

TEST(UndoConsequences, stolen_note_snapshot_is_valid_even_when_cloning_would_fail) {
	NoteVector saved;
	saved.values = {4, 5};
	NoteVector::fail_clone = true;
	ConsequenceNoteArrayChange c(&target, 7, &saved, true);
	CHECK_TRUE(c.snapshot_valid());
	CHECK_TRUE(saved.values.empty());
	CHECK_EQUAL(Error::NONE, c.revert(BEFORE, &stack));
	CHECK_EQUAL(4, row.notes.values.front());
	CHECK_EQUAL(Error::NONE, c.revert(AFTER, &stack));
	CHECK_EQUAL(1, row.notes.values.front());
}

TEST(UndoConsequences, unknown_row_id_never_falls_back_to_another_row) {
	ConsequenceNoteRowLength length(&target, 8, 48);
	ConsequenceNoteRowMute mute(&target, 8);
	ConsequenceNoteRowHorizontalShift shift(&target, 8, 12, true, true);
	NoteVector saved;
	ConsequenceNoteArrayChange notes(&target, 8, &saved, false);
	for (Consequence* c : std::initializer_list<Consequence*>{&length, &mute, &shift, &notes}) {
		for (auto time : {BEFORE, AFTER})
			CHECK_EQUAL(Error::BUG, c->revert(time, &stack));
	}
	CHECK_EQUAL(0, row.length_calls);
	CHECK_EQUAL(0, row.mute_calls);
	CHECK_EQUAL(0, target.row_shift_calls);
	CHECK_EQUAL(1, row.notes.values.front());
}

TEST(UndoConsequences, replacement_at_same_row_id_rejects_all_old_row_consequences) {
	NoteVector snapshot;
	snapshot.values = {99};
	ConsequenceNoteArrayChange notes(&target, 7, &snapshot, false);
	ConsequenceNoteRowHorizontalShift shift(&target, 7, 1, true, true);
	ConsequenceNoteRowLength length(&target, 7, 48);
	ConsequenceNoteRowMute mute(&target, 7);
	// Reconstruct in the same storage, as can happen after array deletion/insertion.
	auto old_identity = row.undo_identity;
	row.~NoteRow();
	new (&row) NoteRow();
	CHECK_TRUE(row.undo_identity != old_identity);
	row.notes.values = {55};
	for (Consequence* c :
	     std::initializer_list<Consequence*>{static_cast<Consequence*>(&notes), &shift, &length, &mute}) {
		CHECK_EQUAL(Error::BUG, c->revert(BEFORE, &stack));
		CHECK_EQUAL(Error::BUG, c->revert(AFTER, &stack));
	}
	CHECK_EQUAL(0, row.length_calls);
	CHECK_EQUAL(0, row.mute_calls);
	CHECK_EQUAL(0, target.row_shift_calls);
	CHECK_EQUAL(55, row.notes.values.front());
	CHECK_EQUAL(99, notes.backedUpNoteVector.values.front());
}

TEST(UndoConsequences, relocated_row_identity_still_allows_undo) {
	ConsequenceNoteRowHorizontalShift shift(&target, 7, 12, true, true);
	// Storage movement preserves identity, unlike constructing a replacement.
	other_row.undo_identity = row.undo_identity;
	target.row = &other_row;
	CHECK_EQUAL(Error::NONE, shift.revert(BEFORE, &stack));
	CHECK_EQUAL(Error::NONE, shift.revert(AFTER, &stack));
	CHECK_EQUAL(2, target.row_shift_calls);
}

TEST(UndoConsequences, row_absent_at_capture_cannot_bind_to_later_creation) {
	target.row = nullptr;
	ConsequenceNoteRowMute mute(&target, 7);
	target.row = &row;
	CHECK_EQUAL(Error::BUG, mute.revert(BEFORE, &stack));
	CHECK_EQUAL(0, row.mute_calls);
}

TEST(UndoConsequences, row_length_failure_preserves_saved_length_for_retry) {
	ConsequenceNoteRowLength consequence(&target, 7, 48);
	for (auto error : {Error::BUG, Error::INSUFFICIENT_RAM}) {
		row.length_error = error;
		CHECK_EQUAL(error, consequence.revert(BEFORE, &stack));
		CHECK_EQUAL(48, consequence.backedUpLength);
		CHECK_EQUAL(0, row.loopLengthIfIndependent);
	}
	row.length_error = Error::NONE;
	CHECK_EQUAL(Error::NONE, consequence.revert(BEFORE, &stack));
	CHECK_EQUAL(48, row.loopLengthIfIndependent);
	CHECK_EQUAL(96, consequence.backedUpLength);
	row.length_error = Error::INSUFFICIENT_RAM;
	CHECK_EQUAL(Error::INSUFFICIENT_RAM, consequence.revert(AFTER, &stack));
	CHECK_EQUAL(96, consequence.backedUpLength);
	CHECK_EQUAL(48, row.loopLengthIfIndependent);
	row.length_error = Error::NONE;
	CHECK_EQUAL(Error::NONE, consequence.revert(AFTER, &stack));
	CHECK_EQUAL(96, row.loopLengthIfIndependent);
	CHECK_EQUAL(48, consequence.backedUpLength);
}

TEST(UndoConsequences, direct_row_length_change_rejects_mismatched_context) {
	ConsequenceNoteRowLength consequence(&target, 7, 48);
	ModelStackWithNoteRow edit{&song, &target, &row};
	CHECK_EQUAL(Error::BUG, consequence.performChange(nullptr, nullptr, 0, false));
	edit.song = nullptr;
	CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
	edit.song = &song;
	edit.clip = &other;
	CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
	edit.clip = &target;
	edit.row = &other_row;
	CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
	edit.row = &row;
	edit.noteRowId = 8;
	CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
	CHECK_EQUAL(0, row.length_calls);
	CHECK_EQUAL(0, other_row.length_calls);
	CHECK_EQUAL(48, consequence.backedUpLength);
}
TEST(UndoConsequences, direct_row_length_change_rejects_replaced_or_detached_target) {
	ConsequenceNoteRowLength consequence(&target, 7, 48);
	ModelStackWithNoteRow edit{&song, &target, &row};
	row.undo_identity++;
	CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
	row.undo_identity--;
	song.registered.clear();
	CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
	CHECK_EQUAL(0, row.length_calls);
	CHECK_EQUAL(48, consequence.backedUpLength);
}
TEST(UndoConsequences, direct_row_length_change_accepts_retained_target) {
	ConsequenceNoteRowLength consequence(&target, 7, 48);
	ModelStackWithNoteRow edit{&song, &target, &row};
	CHECK_EQUAL(Error::NONE, consequence.performChange(&edit, nullptr, 0, false));
	CHECK_EQUAL(1, row.length_calls);
	CHECK_EQUAL(48, row.loopLengthIfIndependent);
	CHECK_EQUAL(96, consequence.backedUpLength);
}

TEST(UndoConsequences, row_length_callback_invalidation_preserves_saved_length) {
	for (int change = 0; change < 4; ++change) {
		ConsequenceNoteRowLength consequence(&target, 7, 48);
		row.on_length = [&] {
			switch (change) {
			case 0:
				target.row = &other_row;
				break;
			case 1:
				song.registered.clear();
				break;
			case 2:
				row.undo_identity++;
				break;
			case 3:
				deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
				    .structural_refresh.request();
				break;
			}
		};
		CHECK_EQUAL(Error::BUG, consequence.revert(BEFORE, &stack));
		CHECK_EQUAL(48, consequence.backedUpLength);
		row.on_length = {};
		target.output = &output;
		other.output = &output;
		target.row = &row;
		song.registered = {&target, &other};
		row.loopLengthIfIndependent = 0;
	}
}

TEST(UndoConsequences, row_length_rejects_callback_changes_to_resize_result_or_parent) {
	for (int change = 0; change < 5; ++change) {
		ConsequenceNoteRowLength consequence(&target, 7, 48);
		ModelStackWithNoteRow edit{&song, &target, &row};
		Output replacement_output;
		auto* oldOutput = target.output;
		row.on_length = [&] {
			switch (change) {
			case 0:
				target.loopLength = 192;
				break;
			case 1:
				target.output = &replacement_output;
				break;
			case 2:
				edit.noteRowId = 8;
				break;
			case 3:
				row.loopLengthIfIndependent = 24;
				break;
			case 4:
				target.type = ClipType::AUDIO;
				break;
			}
		};
		CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
		CHECK_EQUAL(48, consequence.backedUpLength);
		row.on_length = {};
		target.loopLength = 96;
		target.output = oldOutput;
		target.type = ClipType::INSTRUMENT;
		row.loopLengthIfIndependent = 0;
	}
}
TEST(UndoConsequences, row_length_accepts_requested_length_inherited_from_parent) {
	ConsequenceNoteRowLength consequence(&target, 7, 96);
	row.loopLengthIfIndependent = 48;
	row.on_length = [&] { row.loopLengthIfIndependent = 0; };
	ModelStackWithNoteRow edit{&song, &target, &row};
	CHECK_EQUAL(Error::NONE, consequence.performChange(&edit, nullptr, 0, true));
	CHECK_EQUAL(48, consequence.backedUpLength);
	CHECK_EQUAL(0, row.loopLengthIfIndependent);
}

TEST(UndoConsequences, row_length_callback_can_release_original_row_without_post_call_access) {
	auto victim = std::make_unique<NoteRow>();
	target.row = victim.get();
	ConsequenceNoteRowLength consequence(&target, 7, 48);
	ModelStackWithNoteRow edit{&song, &target, victim.get()};
	victim->on_length = [&] {
		target.output = &output;
		other.output = &output;
		target.row = &row;
		victim.reset();
	};
	CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
	CHECK_TRUE(victim == nullptr);
	CHECK_EQUAL(48, consequence.backedUpLength);
	CHECK_EQUAL(0, row.length_calls);
}
TEST(UndoConsequences, row_length_callback_can_release_detached_clip_without_post_call_access) {
	auto victim = std::make_unique<InstrumentClip>();
	victim->row = &row;
	song.registered = {victim.get(), &other};
	ConsequenceNoteRowLength consequence(victim.get(), 7, 48);
	ModelStackWithNoteRow edit{&song, victim.get(), &row};
	row.on_length = [&] {
		song.registered = {&other};
		victim.reset();
	};
	CHECK_EQUAL(Error::BUG, consequence.performChange(&edit, nullptr, 0, false));
	CHECK_TRUE(victim == nullptr);
	CHECK_EQUAL(48, consequence.backedUpLength);
}

TEST(UndoConsequences, missing_output_rejects_length_undo_before_marker_mutation) {
	AudioClip audio;
	song.registered.push_back(&audio);
	for (auto marker : {ConsequenceClipLength::SampleMarker::NONE, ConsequenceClipLength::SampleMarker::START,
	                    ConsequenceClipLength::SampleMarker::END}) {
		for (auto time : {BEFORE, AFTER}) {
			ConsequenceClipLength consequence(&audio, 48);
			consequence.sample_marker = marker;
			consequence.markerValueToRevertTo = 50;
			CHECK_EQUAL(Error::BUG, consequence.revert(time, &stack));
			CHECK_EQUAL(96, audio.loopLength);
			CHECK_EQUAL(10, audio.sampleHolder.startPos);
			CHECK_EQUAL(100, audio.sampleHolder.endPos);
			CHECK_EQUAL(48, consequence.lengthToRevertTo);
			CHECK_EQUAL(50, consequence.markerValueToRevertTo);
			CHECK_EQUAL(0, song.length_calls);
		}
	}
	// A refused operation must remain usable when its output is restored.
	ConsequenceClipLength consequence(&audio, 48);
	consequence.sample_marker = ConsequenceClipLength::SampleMarker::START;
	consequence.markerValueToRevertTo = 50;
	CHECK_EQUAL(Error::BUG, consequence.revert(BEFORE, &stack));
	audio.output = &output;
	CHECK_EQUAL(Error::NONE, consequence.revert(BEFORE, &stack));
	CHECK_EQUAL(50, audio.sampleHolder.startPos);
	CHECK_EQUAL(Error::NONE, consequence.revert(AFTER, &stack));
	CHECK_EQUAL(10, audio.sampleHolder.startPos);
	CHECK_EQUAL(96, audio.loopLength);
}
