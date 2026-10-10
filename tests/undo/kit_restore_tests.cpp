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

TEST(KitRestore, instrument_restoration_rejects_foreign_timeline_before_callbacks) {
	Output output;
	InstrumentClip other;
	clip.output = &output;
	int restore_calls = 0;
	clip.on_midi_restore = [&] { ++restore_calls; };
	for (auto type : {OutputType::MIDI_OUT, OutputType::CV, OutputType::KIT}) {
		output.type = type;
		for (Clip* target : {static_cast<Clip*>(nullptr), static_cast<Clip*>(&other)}) {
			stack.clip = target;
			CHECK(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
		}
	}
	LONGS_EQUAL(0, restore_calls);
	LONGS_EQUAL(0, first.trims);
}

TEST(KitRestore, midi_restoration_rejects_changed_stack_or_output) {
	Output output, other_output;
	Song other_song;
	InstrumentClip other_clip;
	output.type = OutputType::MIDI_OUT;
	for (int change = 0; change < 3; ++change) {
		clip.output = &output;
		stack.song = &song;
		stack.clip = &clip;
		clip.on_midi_restore = [&] {
			if (change == 0)
				stack.song = &other_song;
			if (change == 1)
				stack.clip = &other_clip;
			if (change == 2)
				clip.output = &other_output;
		};
		CHECK(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
	}
}

TEST(KitRestore, base_restoration_rejects_changed_stack_or_output) {
	Output output, other_output;
	Song other_song;
	InstrumentClip other_clip;
	for (int change = 0; change < 3; ++change) {
		clip.output = &output;
		stack.song = &song;
		stack.clip = &clip;
		song.backedUpParamManagers.values = {{&output.mod, &clip, {11, 12}}};
		clip.paramManager.on_trim = [&] {
			if (change == 0)
				stack.song = &other_song;
			if (change == 1)
				stack.clip = &other_clip;
			if (change == 2)
				clip.output = &other_output;
		};
		CHECK(clip.Clip::undoDetachmentFromOutput(&stack) == Error::BUG);
	}
}

TEST(KitRestore, restoration_rejects_owner_switch_but_accepts_returned_scope) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	Output output;
	clip.output = &output;
	output.type = OutputType::MIDI_OUT;
	clip.on_midi_restore = [&] { remote.emplace(Id::Remote); };
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
	remote.reset();
	clip.on_midi_restore = [] { Scope temporary(Id::Remote); };
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::NONE);

	song.backedUpParamManagers.values = {{&output.mod, &clip, {11, 12}}};
	clip.paramManager.on_trim = [&] { remote.emplace(Id::Remote); };
	CHECK(clip.Clip::undoDetachmentFromOutput(&stack) == Error::BUG);
	remote.reset();
	song.backedUpParamManagers.values = {{&output.mod, &clip, {11, 12}}};
	clip.paramManager.on_trim = [] { Scope temporary(Id::Remote); };
	CHECK(clip.Clip::undoDetachmentFromOutput(&stack) == Error::NONE);
}

TEST(KitRestore, row_restoration_rejects_foreign_timeline_without_consuming_backups) {
	InstrumentClip other_clip;
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
	for (Clip* target : {static_cast<Clip*>(nullptr), static_cast<Clip*>(&other_clip)}) {
		stack.clip = target;
		CHECK(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
		LONGS_EQUAL(2, song.backedUpParamManagers.values.size());
		LONGS_EQUAL(0, first.paramManager.main);
		LONGS_EQUAL(0, first.trims);
	}
}

TEST(KitRestore, row_restoration_stops_before_next_backup_when_target_changes) {
	Song other_song;
	InstrumentClip other_clip;
	Output output, other_output;
	for (int change = 0; change < 3; ++change) {
		stack.song = &song;
		stack.clip = &clip;
		clip.output = &output;
		song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
		first.on_trim = [&] {
			if (change == 0)
				stack.song = &other_song;
			if (change == 1)
				stack.clip = &other_clip;
			if (change == 2)
				clip.output = &other_output;
		};
		CHECK(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
		LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
		LONGS_EQUAL(21, song.backedUpParamManagers.values[0].paramManager.main);
		LONGS_EQUAL(0, second.paramManager.main);
		LONGS_EQUAL(0, second.trims);
	}
}

TEST(KitRestore, row_restoration_stops_on_owner_switch_and_accepts_returned_scope) {
	using namespace deluge::gui::ui_session;
	Scope local(Id::Local);
	std::optional<Scope> remote;
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
	first.on_trim = [&] { remote.emplace(Id::Remote); };
	CHECK(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(0, second.trims);
	remote.reset();
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
	first.on_trim = [] { Scope temporary(Id::Remote); };
	CHECK(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::NONE);
	LONGS_EQUAL(0, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(1, second.trims);
}

TEST(KitRestore, kit_reattachment_keeps_remaining_row_and_kit_backups_after_target_change) {
	Output output, other_output;
	output.type = OutputType::KIT;
	clip.output = &output;
	song.backedUpParamManagers.values = {
	    {&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}, {&output.mod, &clip, {31, 32}}};
	first.on_trim = [&] { clip.output = &other_output; };
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
	LONGS_EQUAL(2, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(21, song.backedUpParamManagers.values[0].paramManager.main);
	LONGS_EQUAL(31, song.backedUpParamManagers.values[1].paramManager.main);
	LONGS_EQUAL(0, second.paramManager.main);
	LONGS_EQUAL(0, clip.paramManager.main);
	LONGS_EQUAL(0, second.trims);
}

TEST(KitRestore, row_restoration_stops_when_trim_changes_row_membership_or_identity) {
	NoteRow replacement;
	SoundDrum replacement_drum;
	const auto original_identity = first.undo_identity;
	for (int change = 0; change < 4; ++change) {
		clip.noteRows.values = {&first, &second};
		first.undo_identity = original_identity;
		first.drum = &firstDrum;
		song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
		first.on_trim = [&] {
			if (change == 0)
				clip.noteRows.values = {&second};
			if (change == 1)
				clip.noteRows.values[0] = &replacement;
			if (change == 2)
				++first.undo_identity;
			if (change == 3)
				first.drum = &replacement_drum;
		};
		CHECK(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
		LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
		LONGS_EQUAL(21, song.backedUpParamManagers.values[0].paramManager.main);
		LONGS_EQUAL(0, second.paramManager.main);
		LONGS_EQUAL(0, second.trims);
		LONGS_EQUAL(0, replacement.paramManager.main);
	}
}

TEST(KitRestore, missing_later_backup_after_trim_does_not_consume_intermediate_row) {
	NoteRow third;
	SoundDrum third_drum;
	third.drum = &third_drum;
	clip.noteRows.values = {&first, &second, &third};
	song.backedUpParamManagers.values = {
	    {&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}, {&third_drum, &clip, {31, 32}}};
	first.on_trim = [&] { song.backedUpParamManagers.values.pop_back(); };
	CHECK(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(21, song.backedUpParamManagers.values[0].paramManager.main);
	LONGS_EQUAL(0, second.paramManager.main);
	LONGS_EQUAL(0, second.trims);
	LONGS_EQUAL(0, third.trims);
}

TEST(KitRestore, duplicate_later_assignment_after_trim_preserves_remaining_backups) {
	NoteRow third;
	third.drum = nullptr;
	clip.noteRows.values = {&first, &second, &third};
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
	first.on_trim = [&] { third.drum = &second_drum; };
	CHECK(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(21, song.backedUpParamManagers.values[0].paramManager.main);
	LONGS_EQUAL(0, second.paramManager.main);
	LONGS_EQUAL(0, second.trims);
}

TEST(KitRestore, duplicate_of_restored_row_rejects_even_a_reintroduced_backup) {
	song.backedUpParamManagers.values = {{&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}};
	first.on_trim = [&] {
		second.drum = &firstDrum;
		song.backedUpParamManagers.values.push_back({&firstDrum, &clip, {31, 32}});
	};
	CHECK(clip.undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(2, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(0, second.paramManager.main);
	LONGS_EQUAL(0, second.trims);
}

TEST(KitRestore, kit_backup_invalidated_during_first_trim_preserves_later_rows) {
	Output output;
	output.type = OutputType::KIT;
	clip.output = &output;
	for (bool remove_backup : {false, true}) {
		song.backedUpParamManagers.values = {
		    {&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}, {&output.mod, &clip, {31, 32}}};
		first.on_trim = [&] {
			if (remove_backup)
				song.backedUpParamManagers.values.pop_back();
			else
				song.backedUpParamManagers.values.back().paramManager.valid = false;
		};
		CHECK(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
		LONGS_EQUAL(remove_backup ? 1 : 2, song.backedUpParamManagers.values.size());
		LONGS_EQUAL(21, song.backedUpParamManagers.values[0].paramManager.main);
		LONGS_EQUAL(0, second.paramManager.main);
		LONGS_EQUAL(0, second.trims);
		LONGS_EQUAL(0, clip.paramManager.main);
	}
}

TEST(KitRestore, intact_kit_backup_restores_after_all_rows) {
	Output output;
	output.type = OutputType::KIT;
	clip.output = &output;
	song.backedUpParamManagers.values = {
	    {&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}, {&output.mod, &clip, {31, 32}}};
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::NONE);
	LONGS_EQUAL(0, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(11, first.paramManager.main);
	LONGS_EQUAL(21, second.paramManager.main);
	LONGS_EQUAL(31, clip.paramManager.main);
	LONGS_EQUAL(32, clip.paramManager.expression);
	LONGS_EQUAL(1, first.trims);
	LONGS_EQUAL(1, second.trims);
}

TEST(KitRestore, midi_restoration_rejects_destroyed_registered_output) {
	auto* output = new Output;
	output->type = OutputType::MIDI_OUT;
	clip.output = output;
	song.firstOutput = output;
	clip.on_midi_restore = [&] {
		song.firstOutput = nullptr;
		delete output;
	};
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
}

TEST(KitRestore, base_restoration_rejects_output_removed_by_trim) {
	Output output;
	clip.output = &output;
	song.firstOutput = &output;
	song.backedUpParamManagers.values = {{&output.mod, &clip, {11, 12}}};
	clip.paramManager.on_trim = [&] { song.firstOutput = nullptr; };
	CHECK(clip.Clip::undoDetachmentFromOutput(&stack) == Error::BUG);
}

TEST(KitRestore, kit_restoration_stops_before_backup_check_when_output_destroyed) {
	auto* output = new Output;
	output->type = OutputType::KIT;
	clip.output = output;
	song.firstOutput = output;
	song.backedUpParamManagers.values = {
	    {&firstDrum, &clip, {11, 12}}, {&second_drum, &clip, {21, 22}}, {&output->mod, &clip, {31, 32}}};
	first.on_trim = [&] {
		song.firstOutput = nullptr;
		delete output;
	};
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::BUG);
	LONGS_EQUAL(2, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(0, second.trims);
	LONGS_EQUAL(0, second.paramManager.main);
	LONGS_EQUAL(0, clip.paramManager.main);
}

TEST(KitRestore, midi_restoration_accepts_output_transferred_to_undo_retention) {
	Output output;
	output.type = OutputType::MIDI_OUT;
	clip.output = &output;
	song.firstOutput = &output;
	clip.on_midi_restore = [&] {
		song.firstOutput = nullptr;
		song.undo_detached_outputs.retain(&output);
	};
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::NONE);
	song.undo_detached_outputs.release(&output);
}

TEST(KitRestore, restoration_rejects_registered_clip_removed_without_revision_change) {
	Output output;
	clip.output = &output;
	output.type = OutputType::MIDI_OUT;
	song.sessionClips.values = {&clip};
	clip.on_midi_restore = [&] { song.sessionClips.values.clear(); };
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::BUG);

	song.sessionClips.values = {&clip};
	song.backedUpParamManagers.values = {{&output.mod, &clip, {11, 12}}};
	clip.paramManager.on_trim = [&] { song.sessionClips.values.clear(); };
	CHECK(clip.Clip::undoDetachmentFromOutput(&stack) == Error::BUG);
}

TEST(KitRestore, row_restoration_does_not_read_clip_destroyed_without_revision_change) {
	auto* target = new InstrumentClip;
	target->noteRows.values = {&first, &second};
	stack.clip = target;
	song.sessionClips.values = {target};
	song.backedUpParamManagers.values = {{&firstDrum, target, {11, 12}}, {&second_drum, target, {21, 22}}};
	first.on_trim = [&] {
		song.sessionClips.values.clear();
		delete target;
	};
	CHECK(target->undoUnassignmentOfAllNoteRowsFromDrums(&stack) == Error::BUG);
	LONGS_EQUAL(1, song.backedUpParamManagers.values.size());
	LONGS_EQUAL(0, second.trims);
	LONGS_EQUAL(0, second.paramManager.main);
}

TEST(KitRestore, restoration_accepts_clip_moved_between_song_arrays) {
	Output output;
	output.type = OutputType::MIDI_OUT;
	clip.output = &output;
	song.sessionClips.values = {&clip};
	clip.on_midi_restore = [&] {
		song.sessionClips.values.clear();
		song.arrangementOnlyClips.values = {&clip};
	};
	CHECK(clip.undoDetachmentFromOutput(&stack) == Error::NONE);
}
