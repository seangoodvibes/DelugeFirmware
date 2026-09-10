#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include "undo_model.h"

// Production restoration and Song lookup operate on controlled row/backup storage.
#include "clip_restore_method.inc"
#include "kit_restore_method.inc"

TEST_GROUP(KitRestore) {
	Song song;
	InstrumentClip clip;
	NoteRow first, second;
	SoundDrum firstDrum, second_drum;
	ModelStackWithTimelineCounter stack;
	void setup() override {
		currentSong = &song;
		stack.song = &song;
		stack.clip = &clip;
		first.drum = &firstDrum;
		second.drum = &second_drum;
		clip.noteRows.values = {&first, &second};
	}
	void teardown() override {
		currentSong = nullptr;
	}
};

TEST(KitRestore, missing_later_backup_does_not_consume_earlier_rows) {
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}};
	CHECK_TRUE(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(11, song.backedUpParamManagers.values[0].paramManager.main);
	LONGS_EQUAL(0, first.paramManager.main);
	LONGS_EQUAL(0, first.trims);
}

TEST(KitRestore, duplicate_drum_cannot_consume_one_backup_twice) {
	second.drum = &firstDrum;
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}};
	CHECK_TRUE(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(0, first.trims);
	LONGS_EQUAL(0, second.trims);
}

TEST(KitRestore, complete_backups_restore_each_row_including_expression) {
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
	CHECK_TRUE(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::NONE);
	LONGS_EQUAL(0, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(11, first.paramManager.main);
	LONGS_EQUAL(12, first.paramManager.expression);
	LONGS_EQUAL(21, second.paramManager.main);
	LONGS_EQUAL(22, second.paramManager.expression);
	LONGS_EQUAL(1, first.trims);
	LONGS_EQUAL(1, second.trims);
}

TEST(KitRestore, midi_and_unassigned_rows_need_no_backup) {
	first.drum = nullptr;
	second_drum.type = DrumType::MIDI;
	CHECK_TRUE(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::NONE);
	LONGS_EQUAL(0, first.trims);
	LONGS_EQUAL(0, second.trims);
}

TEST(KitRestore, missing_context_returns_error_before_accessing_backups) {
	CHECK_TRUE(clip.undoUnassignmentOfAllNoteRowsFromDrums(nullptr) == Error::BUG);
	stack.song = nullptr;
	CHECK_TRUE(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(0, first.trims);
}

TEST(KitRestore, song_change_while_trimming_stops_before_next_transfer) {
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
	first.on_trim = [] { currentSong = nullptr; };
	CHECK_TRUE(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(0, second.trims);
	LONGS_EQUAL(0, second.paramManager.main);
}

TEST(KitRestore, either_panels_structural_change_stops_before_next_transfer) {
	using namespace deluge::gui::ui_session;
	for (auto owner : {Id::Local, Id::Remote}) {
		song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
		first.on_trim = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_TRUE(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
		LONGS_EQUAL(0, second.trims);
		LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	}
}

TEST(KitRestore, clip_destroyed_while_trimming_is_not_dereferenced_after_callback) {
	using namespace deluge::gui::ui_session;
	auto* target = new InstrumentClip;
	target->noteRows.values = {&first, &second};
	stack.clip = target;
	song.backedUpParamManagers.values = {{&firstDrum, target, {11, 12}}, {&second_drum, target, {21, 22}}};
	first.on_trim = [target] {
		navigation.for_owner(Id::Remote).structural_refresh.request();
		delete target;
	};
	CHECK_TRUE(target->undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(0, second.trims);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
}

TEST(KitRestore, midi_restore_reports_invalid_parameters_without_freezing) {
	Output output;
	output.type = OutputType::MIDI_OUT;
	clip.output = &output;
	clip.paramManager.valid = false;
	CHECK_TRUE(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
	clip.paramManager.valid = true;
	CHECK_TRUE(clip.undoDetachmentFromOutput(&stack) == Error::NONE);
}

TEST(KitRestore, midi_restore_rejects_changed_song_or_either_panel) {
	using namespace deluge::gui::ui_session;
	Output output;
	output.type = OutputType::MIDI_OUT;
	clip.output = &output;
	for (auto owner : {Id::Local, Id::Remote}) {
		clip.on_midi_restore = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_TRUE(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
	}
	clip.on_midi_restore = [] { currentSong = nullptr; };
	CHECK_TRUE(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
}

TEST(KitRestore, midi_restore_does_not_read_destroyed_clip_after_invalidation) {
	using namespace deluge::gui::ui_session;
	Output output;
	output.type = OutputType::MIDI_OUT;
	auto* target = new InstrumentClip;
	target->output = &output;
	stack.clip = target;
	target->on_midi_restore = [target] {
		navigation.for_owner(Id::Remote).structural_refresh.request();
		delete target;
	};
	CHECK_TRUE(target->undoDetachmentFromOutput(&stack) == Error::BUG);
}

TEST(KitRestore, base_restoration_validates_context_before_consuming_backup) {
	Output output;
	clip.output = &output;
	song.backedUpParamManagers.values = {{&output.mod, &clip, {11, 12}}};
	stack.clip = nullptr;
	CHECK_TRUE(clip.Clip::undoDetachmentFromOutput(&stack) == Error::BUG);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	stack.clip = &clip;
	CHECK_TRUE(clip.Clip::undoDetachmentFromOutput(&stack) == Error::NONE);
	LONGS_EQUAL(11, clip.paramManager.main);
	LONGS_EQUAL(12, clip.paramManager.expression);
}

TEST(KitRestore, base_restoration_reports_trim_invalidation) {
	using namespace deluge::gui::ui_session;
	Output output;
	clip.output = &output;
	for (auto owner : {Id::Local, Id::Remote}) {
		song.backedUpParamManagers.values = {{&output.mod, &clip, {11, 12}}};
		clip.paramManager.on_trim = [owner] { navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_TRUE(clip.Clip::undoDetachmentFromOutput(&stack) == Error::BUG);
	}
	song.backedUpParamManagers.values = {{&output.mod, &clip, {11, 12}}};
	clip.paramManager.on_trim = [] { currentSong = nullptr; };
	CHECK_TRUE(clip.Clip::undoDetachmentFromOutput(&stack) == Error::BUG);
}

TEST(KitRestore, base_restoration_returns_after_clip_deleted_during_trim) {
	using namespace deluge::gui::ui_session;
	Output output;
	auto* target = new Clip;
	target->output = &output;
	stack.clip = target;
	song.backedUpParamManagers.values = {{&output.mod, target, {11, 12}}};
	target->paramManager.on_trim = [target] {
		navigation.for_owner(Id::Local).structural_refresh.request();
		delete target;
	};
	CHECK_TRUE(target->undoDetachmentFromOutput(&stack) == Error::BUG);
}
