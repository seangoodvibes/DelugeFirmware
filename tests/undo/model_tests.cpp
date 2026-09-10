#include "CppUTest/TestHarness.h"
#include "model/clip/sample_shift.h"
#include "undo_model.h"

// Compile production definitions only after the collaborator declarations.
#include "audio_methods.inc"
#include "exact_backup_method.inc"
#include "song_methods.inc"

TEST_GROUP(SharedModelRuntime){};
TEST(SharedModelRuntime, both_clip_registries_count_but_null_and_history_only_clips_do_not) {
	Song song;
	InstrumentClip session, arrangement, detached;
	song.sessionClips.values = {&session};
	song.arrangementOnlyClips.values = {&arrangement};
	CHECK_TRUE(song.contains_clip_for_undo(&session));
	CHECK_TRUE(song.contains_clip_for_undo(&arrangement));
	CHECK_FALSE(song.contains_clip_for_undo(nullptr));
	CHECK_FALSE(song.contains_clip_for_undo(&detached));
	song.sessionClips.values.clear();
	CHECK_FALSE(song.contains_clip_for_undo(&session));
}
TEST(SharedModelRuntime, detached_outputs_cannot_be_deleted_from_active_list_again) {
	Song song;
	Output active, detached, unrelated;
	song.firstOutput = &active;
	song.undo_detached_outputs.retain(&detached);
	CHECK_TRUE(song.owns_output_for_undo(&active, false));
	CHECK_TRUE(song.owns_output_for_undo(&detached));
	CHECK_FALSE(song.owns_output_for_undo(&detached, false));
	CHECK_FALSE(song.owns_output_for_undo(&unrelated));
	CHECK_FALSE(song.owns_output_for_undo(nullptr));
	song.undo_detached_outputs.release(&detached);
	CHECK_FALSE(song.owns_output_for_undo(&detached));
}
TEST(SharedModelRuntime, arrangement_reference_requires_both_membership_and_matching_output) {
	Song song;
	Output owned, other;
	InstrumentClip clip;
	song.firstOutput = &owned;
	song.registered = {&clip};
	clip.output = &other;
	CHECK_FALSE(song.can_reference_clip_from_output(&clip, &owned));
	clip.output = &owned;
	CHECK_TRUE(song.can_reference_clip_from_output(&clip, &owned));
	CHECK_TRUE(song.can_reference_clip_from_output(nullptr, &owned));
	CHECK_FALSE(song.can_reference_clip_from_output(nullptr, &other));
	song.registered.clear();
	CHECK_FALSE(song.can_reference_clip_from_output(&clip, &owned));
}
TEST(SharedModelRuntime, freed_clip_is_rejected_without_dereferencing_it) {
	Song song;
	Output owned;
	song.firstOutput = &owned;
	auto* clip = new InstrumentClip;
	delete clip;
	CHECK_FALSE(song.contains_clip_for_undo(clip));
	CHECK_FALSE(song.can_reference_clip_from_output(clip, &owned));
}

TEST_GROUP(AudioShiftPreflight) {
	AudioClip clip;
	Sample sample;
	void setup() override {
		clip.sampleHolder.audioFile = &sample;
		clip.sampleHolder.startPos = 100;
		clip.sampleHolder.endPos = 196;
	}
};
TEST(AudioShiftPreflight, recording_missing_sample_and_bounds_reject_sample_movement) {
	CHECK_TRUE(clip.can_shift_horizontally(1, true));
	clip.recorder = &sample;
	CHECK_FALSE(clip.can_shift_horizontally(1, true));
	clip.recorder = nullptr;
	clip.sampleHolder.audioFile = nullptr;
	CHECK_FALSE(clip.can_shift_horizontally(1, true));
	clip.sampleHolder.audioFile = &sample;
	CHECK_FALSE(clip.can_shift_horizontally(101, true));
	CHECK_FALSE(clip.can_shift_horizontally(-901, true));
	CHECK_TRUE(clip.can_shift_horizontally(100, true));
	CHECK_TRUE(clip.can_shift_horizontally(-900, true));
}
TEST(AudioShiftPreflight, automation_only_does_not_require_sample_but_still_requires_valid_loop) {
	clip.sampleHolder.audioFile = nullptr;
	clip.recorder = &sample;
	CHECK_TRUE(clip.can_shift_horizontally(100, false));
	clip.loopLength = 0;
	CHECK_FALSE(clip.can_shift_horizontally(100, false));
	clip.loopLength = -1;
	CHECK_FALSE(clip.can_shift_horizontally(100, false));
}
TEST(AudioShiftPreflight, linear_recording_length_is_validated_before_division) {
	clip.linear = true;
	clip.originalLength = 0;
	CHECK_FALSE(clip.can_shift_horizontally(1, true));
	clip.originalLength = 48;
	CHECK_TRUE(clip.can_shift_horizontally(50, true));
	CHECK_FALSE(clip.can_shift_horizontally(51, true));
}
TEST(AudioShiftPreflight, repeated_preflight_does_not_mutate_markers_or_length) {
	for (int amount : {-1000, -1, 0, 1, 1000})
		for (bool sequence : {false, true}) {
			clip.can_shift_horizontally(amount, sequence);
			CHECK_EQUAL(100, clip.sampleHolder.startPos);
			CHECK_EQUAL(196, clip.sampleHolder.endPos);
			CHECK_EQUAL(96, clip.loopLength);
			CHECK_EQUAL(0, clip.shift_calls);
		}
}

TEST(SharedModelRuntime, undo_removal_requires_membership_in_the_exact_owned_array) {
	Song song, other;
	InstrumentClip first, second;
	song.sessionClips.values = {&first, &second};
	song.arrangementOnlyClips.values = {&first};
	LONGS_EQUAL(1, song.get_clip_index_for_undo(&song.sessionClips, &second));
	LONGS_EQUAL(0, song.get_clip_index_for_undo(&song.arrangementOnlyClips, &first));
	LONGS_EQUAL(-1, song.get_clip_index_for_undo(&song.arrangementOnlyClips, &second));
	other.sessionClips.values = {&second};
	LONGS_EQUAL(-1, song.get_clip_index_for_undo(&other.sessionClips, &second));
}

TEST(SharedModelRuntime, undo_removal_rejects_null_and_freed_targets_before_dereference) {
	Song song;
	auto* clip = new InstrumentClip;
	auto* array = new Song::ClipArray;
	delete clip;
	delete array;
	LONGS_EQUAL(-1, song.get_clip_index_for_undo(&song.sessionClips, clip));
	LONGS_EQUAL(-1, song.get_clip_index_for_undo(array, clip));
	LONGS_EQUAL(-1, song.get_clip_index_for_undo(nullptr, clip));
	LONGS_EQUAL(-1, song.get_clip_index_for_undo(&song.sessionClips, nullptr));
}

TEST(SharedModelRuntime, undo_removal_resolves_current_index_after_collection_changes) {
	Song song;
	InstrumentClip first, second;
	song.sessionClips.values = {&first, &second};
	LONGS_EQUAL(1, song.get_clip_index_for_undo(&song.sessionClips, &second));
	song.sessionClips.values.erase(song.sessionClips.values.begin());
	LONGS_EQUAL(0, song.get_clip_index_for_undo(&song.sessionClips, &second));
	song.sessionClips.values.clear();
	LONGS_EQUAL(-1, song.get_clip_index_for_undo(&song.sessionClips, &second));
}

TEST(SharedModelRuntime, exact_backup_lookup_never_consumes_another_clips_or_generic_backup) {
	Song song;
	ModControllableAudio output;
	Clip target, other;
	song.backedUpParamManagers.values = {{&output, &other, {11, 12}}, {&output, nullptr, {21, 22}}};
	ParamManager destination{3, 4};
	POINTERS_EQUAL(nullptr, song.getBackedUpParamManagerForExactClip(&output, &target, &destination));
	LONGS_EQUAL(3, destination.main);
	LONGS_EQUAL(4, destination.expression);
	LONGS_EQUAL(2, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(11, song.backedUpParamManagers.values[0].paramManager.main);
}

TEST(SharedModelRuntime, exact_backup_read_leaves_ownership_and_expression_untouched) {
	Song song;
	ModControllableAudio output, other_output;
	Clip target;
	song.backedUpParamManagers.values = {{&output, &target, {11, 12}}};
	POINTERS_EQUAL(nullptr, song.getBackedUpParamManagerForExactClip(&other_output, &target));
	auto* backup = song.getBackedUpParamManagerForExactClip(&output, &target);
	CHECK_TRUE(backup != nullptr);
	LONGS_EQUAL(11, backup->main);
	LONGS_EQUAL(12, backup->expression);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
}

TEST(SharedModelRuntime, exact_backup_transfer_removes_only_matched_entry_including_expression) {
	Song song;
	ModControllableAudio output;
	Clip target, other;
	song.backedUpParamManagers.values = {{&output, &other, {1, 2}}, {&output, &target, {11, 12}}};
	ParamManager destination;
	POINTERS_EQUAL(&destination, song.getBackedUpParamManagerForExactClip(&output, &target, &destination));
	LONGS_EQUAL(11, destination.main);
	LONGS_EQUAL(12, destination.expression);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	POINTERS_EQUAL(&other, song.backedUpParamManagers.values[0].clip);
	POINTERS_EQUAL(nullptr, song.getBackedUpParamManagerForExactClip(&output, &target));
}

TEST(SharedModelRuntime, exact_backup_rejects_transfer_into_any_backup_entry) {
	Song song;
	ModControllableAudio output;
	Clip target, other;
	song.backedUpParamManagers.values = {{&output, &target, {11, 12}}, {&output, &other, {21, 22}}};
	for (auto& entry : song.backedUpParamManagers.values) {
		POINTERS_EQUAL(nullptr, song.getBackedUpParamManagerForExactClip(&output, &target, &entry.paramManager));
		LONGS_EQUAL(2, song.backedUpParamManagers.values.size());
		LONGS_EQUAL(11, song.backedUpParamManagers.values[0].paramManager.main);
		LONGS_EQUAL(12, song.backedUpParamManagers.values[0].paramManager.expression);
		LONGS_EQUAL(21, song.backedUpParamManagers.values[1].paramManager.main);
		LONGS_EQUAL(22, song.backedUpParamManagers.values[1].paramManager.expression);
	}
}

TEST(SharedModelRuntime, exact_backup_rejects_null_output_and_preserves_generic_lookup) {
	Song song;
	ModControllableAudio output;
	song.backedUpParamManagers.values = {{&output, nullptr, {11, 12}}};
	ParamManager destination{3, 4};
	CHECK_TRUE(song.getBackedUpParamManagerForExactClip(&output, nullptr) != nullptr);
	POINTERS_EQUAL(nullptr, song.getBackedUpParamManagerForExactClip(nullptr, nullptr, &destination));
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(3, destination.main);
	LONGS_EQUAL(4, destination.expression);
}
