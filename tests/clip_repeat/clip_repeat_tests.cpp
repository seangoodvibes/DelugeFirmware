#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

enum class Error { NONE, BUG, INSUFFICIENT_RAM };
static bool fail_snapshot_allocation = false;
static int live_snapshots = 0;
static std::function<void()> on_snapshot_allocation;
struct GeneralMemoryAllocator {
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocMaxSpeed(size_t size) {
		auto callback = on_snapshot_allocation;
		if (callback)
			callback();
		if (fail_snapshot_allocation)
			return nullptr;
		++live_snapshots;
		return ::operator new(size);
	}
};
void delugeDealloc(void* memory) {
	--live_snapshots;
	::operator delete(memory);
}
enum class ClipType { INSTRUMENT, AUDIO };
enum class SequenceDirection { FORWARD, REVERSE, PINGPONG };
enum class IndependentNoteRowLengthIncrease { DOUBLE, ROUND_UP };
struct Action {};
struct Song;
struct Clip;
struct NoteRow;
struct ModelStackWithNoteRow {
	Song* song = nullptr;
	Clip* clip = nullptr;
	NoteRow* row = nullptr;
	int32_t noteRowId = 0;
	int getLastProcessedPos() { return 0; }
	Clip* getTimelineCounter() {
		CHECK_TRUE(clip != nullptr);
		return clip;
	}
	Clip* getTimelineCounterAllowNull() { return clip; }
	NoteRow* getNoteRow() {
		CHECK_TRUE(row != nullptr);
		return row;
	}
	NoteRow* getNoteRowAllowNull() { return row; }
	int32_t getLoopLength();
	bool isCurrentlyPlayingReversed() { return false; }
};
struct ModelStackWithThreeMainThings {};
struct InstrumentClip;
struct Song {
	bool owns_clip = true;
	bool contains_clip_for_undo(void*) { return owns_clip; }
	InstrumentClip* currentClip = nullptr;
	InstrumentClip* getCurrentClip() { return currentClip; }
	int length_sets = 0;
	std::function<void()> on_length_set;
	bool length_change_succeeds = true;
	bool setClipLength(InstrumentClip*, int32_t, Action*);
	InstrumentClip* syncScalingClip = nullptr;
	int scale_changes = 0;
	uint32_t getInputTickScale() { return 1; }
	std::function<void()> on_scale;
	void inputTickScalePotentiallyJustChanged(uint32_t) {
		++scale_changes;
		auto callback = on_scale;
		if (callback)
			callback();
	}
	bool doubleClipLength(InstrumentClip*, Action* = nullptr);
	bool isClipActive(void*) { return true; }
};
Song* currentSong = nullptr;
struct ModelStackWithTimelineCounter {
	Clip* getTimelineCounterAllowNull() { return clip; }
	Song* song;
	Clip* clip = nullptr;
	ModelStackWithNoteRow row;
	ModelStackWithThreeMainThings params;
	ModelStackWithNoteRow* addNoteRow(int row_id, void* target) {
		row = {song, clip, static_cast<NoteRow*>(target), row_id};
		return &row;
	}
	ModelStackWithThreeMainThings* addOtherTwoThingsButNoNoteRow(void*, void*) { return &params; }
};
struct NoteRow {
	uint64_t undo_identity = 1;
	std::function<void()> on_resume;
	std::function<void()> on_repeat;
	std::function<void()> on_trim;
	int32_t loopLengthIfIndependent = 0;
	int repeats = 0, trims = 0, rounded = 0, target = 0;
	int lastProcessedPosIfIndependent = -1;
	bool currentlyPlayingReversedIfIndependent = false;
	int repeatCountIfIndependent = -1;
	int resumed = 0;
	void resumePlayback(ModelStackWithNoteRow*, bool) {
		++resumed;
		auto callback = on_resume;
		if (callback)
			callback();
	}
	Error setLength(ModelStackWithNoteRow*, int32_t, Action*, int32_t, bool);
	bool fail = false;
	bool generateRepeats(ModelStackWithNoteRow*, int32_t, int32_t length, int32_t count, Action*) {
		++repeats;
		rounded = count;
		target = length;
		bool success = !fail;
		auto callback = on_repeat;
		if (callback)
			callback();
		return success;
	}
	Error trimToLength(int32_t, ModelStackWithNoteRow*, Action*) {
		++trims;
		Error result = fail ? Error::BUG : Error::NONE;
		auto callback = on_trim;
		if (callback)
			callback();
		return result;
	}
};
struct Rows {
	std::vector<NoteRow> entries;
	int getNumElements() { return entries.size(); }
	NoteRow* getElement(int i) { return &entries.at(i); }
};
struct Params {
	int repeats = 0;
	std::function<void()> on_repeat;
	void generateRepeats(ModelStackWithThreeMainThings*, int32_t, int32_t, bool) {
		++repeats;
		auto callback = on_repeat;
		if (callback)
			callback();
	}
};
struct Output {
	int length_changes = 0;
	std::function<void()> on_notify;
	void clipLengthChanged(InstrumentClip*, int32_t) {
		++length_changes;
		auto callback = on_notify;
		if (callback)
			callback();
	}
	void* toModControllable() { return this; }
};
struct Clip {
	int32_t loopLength = 16;
	int repeatCount = 3;
	ClipType type = ClipType::INSTRUMENT;
	int changed = 0;
	std::function<void()> on_length_changed;
	void lengthChanged(ModelStackWithTimelineCounter*, int32_t, Action*) {
		++changed;
		auto callback = on_length_changed;
		if (callback)
			callback();
	}
};
struct InstrumentClip : Clip {
	SequenceDirection sequenceDirectionMode = SequenceDirection::PINGPONG;
	Rows noteRows;
	Params paramManager;
	Output storage;
	Output* output = &storage;
	int resumed = 0;
	Error halveNoteRowsWithIndependentLength(ModelStackWithTimelineCounter*);
	int getNoteRowId(NoteRow*, int i) { return i; }
	NoteRow* getNoteRowFromId(int i) {
		return i >= 0 && i < noteRows.getNumElements() ? noteRows.getElement(i) : nullptr;
	}
	std::function<void()> on_resume;
	void resumePlayback(ModelStackWithTimelineCounter*) {
		++resumed;
		auto callback = on_resume;
		if (callback)
			callback();
	}
	bool increaseLengthWithRepeats(ModelStackWithTimelineCounter*, int32_t, IndependentNoteRowLengthIncrease, bool,
	                               Action*);
	bool repeatOrChopToExactLength(ModelStackWithTimelineCounter*, int32_t);
};
int32_t ModelStackWithNoteRow::getLoopLength() {
	return row->loopLengthIfIndependent ? row->loopLengthIfIndependent : clip->loopLength;
}
bool Song::setClipLength(InstrumentClip* clip, int32_t length, Action*) {
	++length_sets;
	clip->loopLength = length;
	auto callback = on_length_set;
	if (callback)
		callback();
	return length_change_succeeds;
}
struct Playback {
	bool isEitherClockActive() { return true; }
} playbackHandler;
struct PlaybackMode {
	int resyncs = 0;
	std::function<void()> on_resync;
	void reSyncClip(ModelStackWithTimelineCounter*) {
		++resyncs;
		auto callback = on_resync;
		if (callback)
			callback();
	}
} playback_mode;
auto* currentPlaybackMode = &playback_mode;
constexpr int MODEL_STACK_MAX_SIZE = 128;
struct StackBuilder {
	ModelStackWithTimelineCounter stack;
	ModelStackWithTimelineCounter* addTimelineCounter(InstrumentClip* clip) {
		stack.clip = clip;
		return &stack;
	}
};
static StackBuilder* setupModelStackWithSong(char*, Song* song) {
	static StackBuilder builder;
	builder.stack.song = song;
	return &builder;
}
bool shouldResumePlaybackOnNoteRowLengthSet = true;
#include "clip_repeat_methods.inc"
#include "row_length_method.inc"
#include "song_double_method.inc"
enum TimeType { BEFORE, AFTER };
struct ModelStack {
	Song* song;
	ModelStackWithTimelineCounter stack;
	ModelStackWithTimelineCounter* addTimelineCounter(InstrumentClip* clip) {
		stack.clip = clip;
		stack.song = song;
		return &stack;
	}
};
struct ConsequenceInstrumentClipMultiply {
	Error revert(TimeType, ModelStack*);
};
#include "multiply_revert_method.inc"

TEST_GROUP(ClipRepeat) {
	Song song;
	ModelStackWithTimelineCounter stack{&song};
	InstrumentClip clip;
	void setup() {
		on_snapshot_allocation = {};
		fail_snapshot_allocation = false;
		currentSong = &song;
		clip.noteRows.entries.resize(2);
		stack.clip = &clip;
		shouldResumePlaybackOnNoteRowLengthSet = true;
		playback_mode.resyncs = 0;
	}
	void teardown() {
		LONGS_EQUAL(0, live_snapshots);
		on_snapshot_allocation = {};
		currentSong = nullptr;
	}
	void untouched() {
		for (auto& row : clip.noteRows.entries) {
			LONGS_EQUAL(0, row.repeats);
			LONGS_EQUAL(0, row.trims);
		}
		LONGS_EQUAL(0, clip.paramManager.repeats);
		LONGS_EQUAL(0, clip.changed);
		LONGS_EQUAL(0, clip.resumed);
		CHECK_TRUE(clip.sequenceDirectionMode == SequenceDirection::PINGPONG);
	}
};
TEST(ClipRepeat, rejects_invalid_clip_lengths_before_any_row) {
	for (int length : {0, -1}) {
		CHECK_FALSE(
		    clip.increaseLengthWithRepeats(&stack, length, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
		CHECK_FALSE(clip.repeatOrChopToExactLength(&stack, length));
		untouched();
		clip.loopLength = length;
		CHECK_FALSE(
		    clip.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
		clip.repeatOrChopToExactLength(&stack, 32);
		untouched();
		clip.loopLength = 16;
	}
}
TEST(ClipRepeat, rejects_invalid_later_row_before_earlier_row_changes) {
	clip.noteRows.entries[1].loopLengthIfIndependent = -1;
	CHECK_FALSE(clip.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
	clip.repeatOrChopToExactLength(&stack, 32);
	untouched();
	LONGS_EQUAL(-1, clip.noteRows.entries[1].loopLengthIfIndependent);
}
TEST(ClipRepeat, rejects_overflowing_independent_targets_before_any_row) {
	clip.noteRows.entries[1].loopLengthIfIndependent = INT32_MAX / 2 + 1;
	CHECK_FALSE(clip.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
	untouched();
	CHECK_FALSE(
	    clip.increaseLengthWithRepeats(&stack, INT32_MAX, IndependentNoteRowLengthIncrease::ROUND_UP, false, nullptr));
	untouched();
}
TEST(ClipRepeat, rounded_repeat_count_does_not_overflow_signed_addition) {
	clip.loopLength = INT32_MAX - 1;
	CHECK_TRUE(clip.repeatOrChopToExactLength(&stack, INT32_MAX));
	LONGS_EQUAL(INT32_MAX, clip.loopLength);
	LONGS_EQUAL(1, clip.noteRows.entries[0].rounded);
	LONGS_EQUAL(1, clip.changed);
	LONGS_EQUAL(1, clip.resumed);
}
TEST(ClipRepeat, increase_rounds_independent_length_and_commits_success) {
	clip.noteRows.entries[1].loopLengthIfIndependent = 12;
	CHECK_TRUE(clip.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::ROUND_UP, false, nullptr));
	LONGS_EQUAL(32, clip.loopLength);
	LONGS_EQUAL(36, clip.noteRows.entries[1].loopLengthIfIndependent);
	LONGS_EQUAL(3, clip.noteRows.entries[1].rounded);
	LONGS_EQUAL(1, clip.paramManager.repeats);
	CHECK_TRUE(clip.sequenceDirectionMode == SequenceDirection::FORWARD);
}
TEST(ClipRepeat, row_failure_stops_clip_commit_and_later_rows) {
	clip.noteRows.entries[0].fail = true;
	CHECK_FALSE(clip.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
	clip.repeatOrChopToExactLength(&stack, 32);
	clip.repeatOrChopToExactLength(&stack, 8);
	LONGS_EQUAL(16, clip.loopLength);
	LONGS_EQUAL(0, clip.noteRows.entries[1].repeats);
	LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
	LONGS_EQUAL(0, clip.paramManager.repeats);
	LONGS_EQUAL(0, clip.changed);
	LONGS_EQUAL(0, clip.resumed);
}

TEST(ClipRepeat, song_doubling_stops_notifications_on_row_failure) {
	song.syncScalingClip = &clip;
	clip.noteRows.entries[0].fail = true;
	song.doubleClipLength(&clip, nullptr);
	LONGS_EQUAL(1, clip.noteRows.entries[0].repeats);
	LONGS_EQUAL(16, clip.loopLength);
	LONGS_EQUAL(0, clip.storage.length_changes);
	LONGS_EQUAL(0, song.scale_changes);
	LONGS_EQUAL(0, playback_mode.resyncs);
}
TEST(ClipRepeat, song_doubling_rejects_overflow_before_repeat_work) {
	for (int length : {0, -1, INT32_MAX / 2 + 1, INT32_MAX}) {
		clip.loopLength = length;
		song.doubleClipLength(&clip, nullptr);
		LONGS_EQUAL(length, clip.loopLength);
		untouched();
	}
	LONGS_EQUAL(0, clip.storage.length_changes);
	LONGS_EQUAL(0, playback_mode.resyncs);
}
TEST(ClipRepeat, song_doubling_notifies_and_resyncs_after_success) {
	song.syncScalingClip = &clip;
	song.doubleClipLength(&clip, nullptr);
	LONGS_EQUAL(32, clip.loopLength);
	LONGS_EQUAL(1, clip.storage.length_changes);
	LONGS_EQUAL(1, song.scale_changes);
	LONGS_EQUAL(1, playback_mode.resyncs);
}

TEST(ClipRepeat, multiply_redo_propagates_failure) {
	song.currentClip = &clip;
	clip.noteRows.entries[0].fail = true;
	ModelStack modelStack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(AFTER, &modelStack) == Error::BUG);
	LONGS_EQUAL(16, clip.loopLength);
	LONGS_EQUAL(0, clip.storage.length_changes);
	LONGS_EQUAL(0, playback_mode.resyncs);
}
TEST(ClipRepeat, multiply_redo_reports_success_after_doubling) {
	song.currentClip = &clip;
	ModelStack modelStack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(AFTER, &modelStack) == Error::NONE);
	LONGS_EQUAL(32, clip.loopLength);
	LONGS_EQUAL(1, clip.storage.length_changes);
	LONGS_EQUAL(1, playback_mode.resyncs);
}

TEST(ClipRepeat, multiply_rejects_missing_or_wrong_clip_context) {
	ConsequenceInstrumentClipMultiply consequence;
	ModelStack modelStack{&song};
	CHECK_TRUE(consequence.revert(BEFORE, nullptr) == Error::BUG);
	ModelStack missing_song{};
	CHECK_TRUE(consequence.revert(AFTER, &missing_song) == Error::BUG);
	CHECK_TRUE(consequence.revert(BEFORE, &modelStack) == Error::BUG);
	song.currentClip = &clip;
	clip.type = ClipType::AUDIO;
	for (TimeType time : {BEFORE, AFTER})
		CHECK_TRUE(consequence.revert(time, &modelStack) == Error::BUG);
	LONGS_EQUAL(0, song.length_sets);
	LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
	untouched();
}
TEST(ClipRepeat, multiply_undo_rejects_invalid_halved_lengths_before_parent_change) {
	song.currentClip = &clip;
	ModelStack modelStack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	for (int length : {-1, 0, 1}) {
		clip.loopLength = length;
		CHECK_TRUE(consequence.revert(BEFORE, &modelStack) == Error::BUG);
		LONGS_EQUAL(length, clip.loopLength);
	}
	clip.loopLength = 16;
	for (int length : {-1, 1}) {
		clip.noteRows.entries[1].loopLengthIfIndependent = length;
		CHECK_TRUE(consequence.revert(BEFORE, &modelStack) == Error::BUG);
		LONGS_EQUAL(16, clip.loopLength);
		LONGS_EQUAL(length, clip.noteRows.entries[1].loopLengthIfIndependent);
	}
	LONGS_EQUAL(0, song.length_sets);
	LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
	untouched();
}
TEST(ClipRepeat, multiply_undo_parent_failure_prevents_row_edits_and_releases_snapshot) {
	song.currentClip = &clip;
	song.length_change_succeeds = false;
	for (auto& row : clip.noteRows.entries)
		row.loopLengthIfIndependent = 2;
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::BUG);
	LONGS_EQUAL(8, clip.loopLength); // The parent mutation is not rolled back.
	LONGS_EQUAL(1, song.length_sets);
	for (auto& row : clip.noteRows.entries) {
		LONGS_EQUAL(2, row.loopLengthIfIndependent);
		LONGS_EQUAL(0, row.trims);
	}
	LONGS_EQUAL(0, live_snapshots);
}

TEST(ClipRepeat, multiply_undo_accepts_inherited_and_positive_halved_lengths) {
	song.currentClip = &clip;
	clip.noteRows.entries[1].loopLengthIfIndependent = 2;
	ModelStack modelStack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(BEFORE, &modelStack) == Error::NONE);
	LONGS_EQUAL(8, clip.loopLength);
	LONGS_EQUAL(1, song.length_sets);
	LONGS_EQUAL(1, clip.noteRows.entries[1].trims);
	LONGS_EQUAL(1, clip.noteRows.entries[1].loopLengthIfIndependent);
}

TEST(ClipRepeat, multiply_undo_propagates_row_length_failure_and_stops_later_rows) {
	song.currentClip = &clip;
	clip.noteRows.entries[0].loopLengthIfIndependent = 8;
	clip.noteRows.entries[1].loopLengthIfIndependent = 12;
	clip.noteRows.entries[0].fail = true;
	ModelStack modelStack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(BEFORE, &modelStack) == Error::BUG);
	LONGS_EQUAL(1, clip.noteRows.entries[0].trims);
	LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
	LONGS_EQUAL(12, clip.noteRows.entries[1].loopLengthIfIndependent);
	// Parent mutation has already happened; this test does not claim rollback.
	LONGS_EQUAL(8, clip.loopLength);
}

TEST(ClipRepeat, real_row_setter_rejects_invalid_length_before_trim_or_playback) {
	auto& row = clip.noteRows.entries[0];
	auto* row_stack = stack.addNoteRow(0, &row);
	for (int length : {0, -1})
		CHECK_TRUE(row.setLength(row_stack, length, nullptr, 25, false) == Error::BUG);
	LONGS_EQUAL(0, row.trims);
	LONGS_EQUAL(0, row.resumed);
	LONGS_EQUAL(0, row.loopLengthIfIndependent);
}
TEST(ClipRepeat, real_row_setter_trim_failure_preserves_playback_state) {
	auto& row = clip.noteRows.entries[0];
	row.fail = true;
	auto* row_stack = stack.addNoteRow(0, &row);
	CHECK_TRUE(row.setLength(row_stack, 8, nullptr, 25, false) == Error::BUG);
	LONGS_EQUAL(0, row.loopLengthIfIndependent);
	LONGS_EQUAL(-1, row.lastProcessedPosIfIndependent);
	LONGS_EQUAL(-1, row.repeatCountIfIndependent);
	LONGS_EQUAL(0, row.resumed);
}
TEST(ClipRepeat, real_row_setter_wraps_position_and_can_restore_inherited_length) {
	auto& row = clip.noteRows.entries[0];
	auto* row_stack = stack.addNoteRow(0, &row);
	CHECK_TRUE(row.setLength(row_stack, 8, nullptr, 25, false) == Error::NONE);
	LONGS_EQUAL(8, row.loopLengthIfIndependent);
	LONGS_EQUAL(1, row.lastProcessedPosIfIndependent);
	LONGS_EQUAL(3, row.repeatCountIfIndependent);
	LONGS_EQUAL(1, row.resumed);
	shouldResumePlaybackOnNoteRowLengthSet = false;
	CHECK_TRUE(row.setLength(row_stack, 16, nullptr, 1, true) == Error::NONE);
	LONGS_EQUAL(0, row.loopLengthIfIndependent);
	LONGS_EQUAL(1, row.resumed);
}

TEST(ClipRepeat, real_row_setter_rejects_missing_or_mismatched_context) {
	auto& row = clip.noteRows.entries[0];
	auto* row_stack = stack.addNoteRow(0, &row);
	CHECK_TRUE(row.setLength(nullptr, 8, nullptr, 25, false) == Error::BUG);
	row_stack->song = nullptr;
	CHECK_TRUE(row.setLength(row_stack, 8, nullptr, 25, false) == Error::BUG);
	row_stack->song = &song;
	row_stack->clip = nullptr;
	CHECK_TRUE(row.setLength(row_stack, 8, nullptr, 25, false) == Error::BUG);
	row_stack->clip = &clip;
	row_stack->row = nullptr;
	CHECK_TRUE(row.setLength(row_stack, 8, nullptr, 25, false) == Error::BUG);
	row_stack->row = &clip.noteRows.entries[1];
	CHECK_TRUE(row.setLength(row_stack, 8, nullptr, 25, false) == Error::BUG);
	LONGS_EQUAL(0, row.trims);
	LONGS_EQUAL(0, row.resumed);
	LONGS_EQUAL(0, row.loopLengthIfIndependent);
	LONGS_EQUAL(-1, row.lastProcessedPosIfIndependent);
}
TEST(ClipRepeat, real_row_setter_rejects_invalid_source_lengths_before_mutation) {
	auto& row = clip.noteRows.entries[0];
	auto* row_stack = stack.addNoteRow(0, &row);
	for (int length : {0, -1}) {
		clip.loopLength = length;
		CHECK_TRUE(row.setLength(row_stack, 8, nullptr, 25, false) == Error::BUG);
	}
	clip.loopLength = 16;
	row.loopLengthIfIndependent = -1;
	CHECK_TRUE(row.setLength(row_stack, 8, nullptr, 25, false) == Error::BUG);
	LONGS_EQUAL(-1, row.loopLengthIfIndependent);
	LONGS_EQUAL(0, row.trims);
	LONGS_EQUAL(0, row.resumed);
	LONGS_EQUAL(-1, row.lastProcessedPosIfIndependent);
}

TEST(ClipRepeat, halving_stops_after_resume_invalidates_clip_ownership) {
	clip.noteRows.entries[0].loopLengthIfIndependent = 8;
	clip.noteRows.entries[1].loopLengthIfIndependent = 12;
	clip.noteRows.entries[0].on_resume = [&] { song.owns_clip = false; };
	CHECK_TRUE(clip.halveNoteRowsWithIndependentLength(&stack) == Error::BUG);
	LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
	LONGS_EQUAL(12, clip.noteRows.entries[1].loopLengthIfIndependent);
}
TEST(ClipRepeat, halving_returns_after_resume_releases_clip) {
	auto victim = std::make_unique<InstrumentClip>();
	victim->noteRows.entries.resize(1);
	victim->noteRows.entries[0].loopLengthIfIndependent = 8;
	stack.clip = victim.get();
	victim->noteRows.entries[0].on_resume = [&] {
		deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote).structural_refresh.request();
		victim.reset();
	};
	CHECK_TRUE(victim->halveNoteRowsWithIndependentLength(&stack) == Error::BUG);
	CHECK_TRUE(victim == nullptr);
}

TEST(ClipRepeat, increase_stops_after_row_callback_invalidation) {
	clip.noteRows.entries[0].on_repeat = [&] { song.owns_clip = false; };
	CHECK_FALSE(clip.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
	LONGS_EQUAL(0, clip.noteRows.entries[1].repeats);
	LONGS_EQUAL(0, clip.paramManager.repeats);
	LONGS_EQUAL(16, clip.loopLength);
}
TEST(ClipRepeat, increase_rejects_parameter_callback_parent_mutation) {
	clip.paramManager.on_repeat = [&] { clip.loopLength = 24; };
	CHECK_FALSE(clip.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
	LONGS_EQUAL(24, clip.loopLength);
	CHECK_TRUE(clip.sequenceDirectionMode == SequenceDirection::PINGPONG);
}
TEST(ClipRepeat, increase_can_return_after_callback_releases_clip) {
	for (bool during_parameters : {false, true}) {
		auto victim = std::make_unique<InstrumentClip>();
		victim->noteRows.entries.resize(1);
		stack.clip = victim.get();
		auto release = [&] {
			deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
			    .structural_refresh.request();
			victim.reset();
		};
		if (during_parameters)
			victim->paramManager.on_repeat = release;
		else
			victim->noteRows.entries[0].on_repeat = release;
		CHECK_FALSE(
		    victim->increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
		CHECK_TRUE(victim == nullptr);
	}
}
TEST(ClipRepeat, increase_supports_unpublished_clone) {
	song.owns_clip = false;
	CHECK_TRUE(clip.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr));
	LONGS_EQUAL(32, clip.loopLength);
}

TEST(ClipRepeat, repeat_or_chop_stops_after_row_ownership_loss) {
	for (int length : {8, 32}) {
		song.owns_clip = true;
		clip.noteRows.entries[0].on_repeat = [&] { song.owns_clip = false; };
		clip.noteRows.entries[0].on_trim = [&] { song.owns_clip = false; };
		CHECK_FALSE(clip.repeatOrChopToExactLength(&stack, length));
		LONGS_EQUAL(16, clip.loopLength);
		LONGS_EQUAL(0, clip.noteRows.entries[1].repeats);
		LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
		LONGS_EQUAL(0, clip.changed);
	}
}
TEST(ClipRepeat, repeat_or_chop_can_return_after_callbacks_release_clip) {
	for (int stage = 0; stage < 5; ++stage) {
		auto victim = std::make_unique<InstrumentClip>();
		victim->noteRows.entries.resize(1);
		stack.clip = victim.get();
		auto release = [&] {
			deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
			    .structural_refresh.request();
			victim.reset();
		};
		if (stage == 0)
			victim->noteRows.entries[0].on_repeat = release;
		if (stage == 1)
			victim->noteRows.entries[0].on_trim = release;
		if (stage == 2)
			victim->paramManager.on_repeat = release;
		if (stage == 3)
			victim->on_length_changed = release;
		if (stage == 4)
			victim->on_resume = release;
		CHECK_FALSE(victim->repeatOrChopToExactLength(&stack, stage == 1 ? 8 : 32));
		CHECK_TRUE(victim == nullptr);
	}
}

TEST(ClipRepeat, multiply_undo_stops_after_length_set_context_changes) {
	Song other_song;
	InstrumentClip other_clip;
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	for (int change = 0; change < 7; ++change) {
		currentSong = &song;
		model_stack.song = &song;
		song.owns_clip = true;
		song.currentClip = &clip;
		clip.loopLength = 16;
		clip.output = &clip.storage;
		clip.noteRows.entries[0].loopLengthIfIndependent = 8;
		song.on_length_set = [&] {
			switch (change) {
			case 0:
				currentSong = &other_song;
				break;
			case 1:
				model_stack.song = &other_song;
				break;
			case 2:
				song.owns_clip = false;
				break;
			case 3:
				song.currentClip = &other_clip;
				break;
			case 4:
				clip.loopLength = 10;
				break;
			case 5:
				clip.output = &other_clip.storage;
				break;
			case 6:
				deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Remote)
				    .structural_refresh.request();
				break;
			}
		};
		CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::BUG);
		LONGS_EQUAL(0, clip.noteRows.entries[0].trims);
		LONGS_EQUAL(8, clip.noteRows.entries[0].loopLengthIfIndependent);
	}
}
TEST(ClipRepeat, multiply_undo_rejects_clip_freed_by_length_set_callback) {
	auto target = std::make_unique<InstrumentClip>();
	target->noteRows.entries.resize(1);
	target->noteRows.entries[0].loopLengthIfIndependent = 8;
	song.currentClip = target.get();
	song.on_length_set = [&] {
		song.owns_clip = false;
		song.currentClip = nullptr;
		target.reset();
	};
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::BUG);
	CHECK_TRUE(target == nullptr);
}
TEST(ClipRepeat, multiply_rejects_unowned_target_before_mutation) {
	song.currentClip = &clip;
	song.owns_clip = false;
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	for (TimeType time : {BEFORE, AFTER})
		CHECK_TRUE(consequence.revert(time, &model_stack) == Error::BUG);
	LONGS_EQUAL(0, song.length_sets);
	LONGS_EQUAL(16, clip.loopLength);
}

TEST(ClipRepeat, halving_preflights_all_lengths_before_changing_any_row) {
	clip.noteRows.entries[0].loopLengthIfIndependent = 8;
	for (int32_t invalid_length : {-1, 1, INT32_MIN}) {
		clip.noteRows.entries[1].loopLengthIfIndependent = invalid_length;
		CHECK_TRUE(clip.halveNoteRowsWithIndependentLength(&stack) == Error::BUG);
		LONGS_EQUAL(8, clip.noteRows.entries[0].loopLengthIfIndependent);
		LONGS_EQUAL(0, clip.noteRows.entries[0].trims);
	}
}
TEST(ClipRepeat, halving_rejects_invalid_parent_even_with_no_rows) {
	clip.noteRows.entries.clear();
	for (int32_t invalid_length : {0, -1}) {
		clip.loopLength = invalid_length;
		CHECK_TRUE(clip.halveNoteRowsWithIndependentLength(&stack) == Error::BUG);
	}
}
TEST(ClipRepeat, multiply_undo_rechecks_later_row_length_after_parent_callback) {
	song.currentClip = &clip;
	clip.noteRows.entries[0].loopLengthIfIndependent = 8;
	clip.noteRows.entries[1].loopLengthIfIndependent = 12;
	song.on_length_set = [&] { clip.noteRows.entries[1].loopLengthIfIndependent = 1; };
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::BUG);
	LONGS_EQUAL(0, clip.noteRows.entries[0].trims);
	LONGS_EQUAL(8, clip.noteRows.entries[0].loopLengthIfIndependent);
	LONGS_EQUAL(1, clip.noteRows.entries[1].loopLengthIfIndependent);
}
TEST(ClipRepeat, multiply_undo_rejects_row_count_changes_after_parent_callback) {
	song.currentClip = &clip;
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	for (int row_count : {1, 3}) {
		clip.loopLength = 16;
		clip.noteRows.entries.resize(2);
		clip.noteRows.entries[0].loopLengthIfIndependent = 8;
		song.on_length_set = [&] { clip.noteRows.entries.resize(row_count); };
		CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::BUG);
		LONGS_EQUAL(0, clip.noteRows.entries[0].trims);
		LONGS_EQUAL(8, clip.noteRows.entries[0].loopLengthIfIndependent);
		LONGS_EQUAL(row_count, clip.noteRows.getNumElements());
	}
}

TEST(ClipRepeat, multiply_undo_snapshot_allocation_failure_preserves_parent_and_rows) {
	song.currentClip = &clip;
	clip.noteRows.entries[0].loopLengthIfIndependent = 8;
	fail_snapshot_allocation = true;
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, song.length_sets);
	LONGS_EQUAL(16, clip.loopLength);
	LONGS_EQUAL(8, clip.noteRows.entries[0].loopLengthIfIndependent);
}
TEST(ClipRepeat, multiply_undo_rejects_same_count_row_changes) {
	song.currentClip = &clip;
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	for (int change = 0; change < 4; ++change) {
		clip.loopLength = 16;
		clip.noteRows.entries[0].undo_identity = 10;
		clip.noteRows.entries[1].undo_identity = 11;
		clip.noteRows.entries[0].loopLengthIfIndependent = 8;
		clip.noteRows.entries[1].loopLengthIfIndependent = 12;
		song.on_length_set = [&] {
			switch (change) {
			case 0:
				++clip.noteRows.entries[1].undo_identity;
				break;
			case 1:
				std::swap(clip.noteRows.entries[0], clip.noteRows.entries[1]);
				break;
			case 2:
				clip.noteRows.entries[1].loopLengthIfIndependent = 10;
				break;
			case 3: {
				auto replacement_rows = clip.noteRows.entries;
				clip.noteRows.entries.swap(replacement_rows);
				break;
			}
			}
		};
		CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::BUG);
		LONGS_EQUAL(0, clip.noteRows.entries[0].trims);
		LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
		LONGS_EQUAL(0, live_snapshots);
	}
}

TEST(ClipRepeat, multiply_undo_allows_parent_length_normalization_to_inherited) {
	song.currentClip = &clip;
	clip.noteRows.entries[0].loopLengthIfIndependent = 8;
	clip.noteRows.entries[1].loopLengthIfIndependent = 12;
	song.on_length_set = [&] { clip.noteRows.entries[0].loopLengthIfIndependent = 0; };
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::NONE);
	LONGS_EQUAL(0, clip.noteRows.entries[0].loopLengthIfIndependent);
	LONGS_EQUAL(6, clip.noteRows.entries[1].loopLengthIfIndependent);
}

TEST(ClipRepeat, multiply_undo_revalidates_after_snapshot_allocation) {
	Song other_song;
	ModelStack model_stack{&song};
	ConsequenceInstrumentClipMultiply consequence;
	for (int change = 0; change < 7; ++change) {
		currentSong = &song;
		model_stack.song = &song;
		song.currentClip = &clip;
		song.owns_clip = true;
		clip.loopLength = 16;
		clip.output = &clip.storage;
		clip.noteRows.entries.resize(2);
		clip.noteRows.entries[1].loopLengthIfIndependent = 8;
		on_snapshot_allocation = [&] {
			switch (change) {
			case 0:
				currentSong = &other_song;
				break;
			case 1:
				model_stack.song = &other_song;
				break;
			case 2:
				song.owns_clip = false;
				break;
			case 3:
				clip.loopLength = 32;
				break;
			case 4:
				clip.noteRows.entries.clear();
				break;
			case 5:
				clip.noteRows.entries[1].loopLengthIfIndependent = 1;
				break;
			case 6:
				deluge::gui::ui_session::navigation.for_owner(deluge::gui::ui_session::Id::Local)
				    .structural_refresh.request();
				break;
			}
		};
		CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::BUG);
		LONGS_EQUAL(0, song.length_sets);
		LONGS_EQUAL(0, live_snapshots);
	}
}
TEST(ClipRepeat, multiply_undo_rejects_target_freed_during_snapshot_allocation) {
	for (bool allocation_fails : {false, true}) {
		auto target = std::make_unique<InstrumentClip>();
		target->noteRows.entries.resize(1);
		song.owns_clip = true;
		song.currentClip = target.get();
		fail_snapshot_allocation = allocation_fails;
		on_snapshot_allocation = [&] {
			song.owns_clip = false;
			song.currentClip = nullptr;
			target.reset();
		};
		ModelStack model_stack{&song};
		ConsequenceInstrumentClipMultiply consequence;
		CHECK_TRUE(consequence.revert(BEFORE, &model_stack) == Error::BUG);
		LONGS_EQUAL(0, song.length_sets);
		LONGS_EQUAL(0, live_snapshots);
		on_snapshot_allocation = {};
	}
}

TEST(ClipRepeat, halving_rejects_resume_length_changes_before_later_rows) {
	for (int32_t callback_length : {0, 3, -1}) {
		clip.noteRows.entries[0].loopLengthIfIndependent = 8;
		clip.noteRows.entries[1].loopLengthIfIndependent = 12;
		clip.noteRows.entries[0].on_resume = [&] {
			clip.noteRows.entries[0].loopLengthIfIndependent = callback_length;
		};
		CHECK_TRUE(clip.halveNoteRowsWithIndependentLength(&stack) == Error::BUG);
		LONGS_EQUAL(callback_length, clip.noteRows.entries[0].loopLengthIfIndependent);
		LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
		LONGS_EQUAL(12, clip.noteRows.entries[1].loopLengthIfIndependent);
	}
}
TEST(ClipRepeat, halving_rejects_resume_row_stack_changes) {
	Song other_song;
	InstrumentClip other_clip;
	for (int change = 0; change < 3; ++change) {
		clip.noteRows.entries[0].loopLengthIfIndependent = 8;
		clip.noteRows.entries[1].loopLengthIfIndependent = 12;
		clip.noteRows.entries[0].on_resume = [&] {
			if (change == 0)
				stack.row.song = &other_song;
			else if (change == 1)
				stack.row.clip = &other_clip;
			else
				stack.row.row = &clip.noteRows.entries[1];
		};
		CHECK_TRUE(clip.halveNoteRowsWithIndependentLength(&stack) == Error::BUG);
		LONGS_EQUAL(0, clip.noteRows.entries[1].trims);
	}
}
TEST(ClipRepeat, halving_accepts_effective_target_inherited_from_parent) {
	clip.loopLength = 4;
	clip.noteRows.entries[0].loopLengthIfIndependent = 8;
	CHECK_TRUE(clip.halveNoteRowsWithIndependentLength(&stack) == Error::NONE);
	LONGS_EQUAL(0, clip.noteRows.entries[0].loopLengthIfIndependent);
	LONGS_EQUAL(1, clip.noteRows.entries[0].resumed);
}

TEST(ClipRepeat, row_setter_rejects_trim_changes_before_playback_state_writes) {
	Song other_song;
	for (int change = 0; change < 6; ++change) {
		currentSong = &song;
		song.owns_clip = true;
		clip.loopLength = 16;
		auto& row = clip.noteRows.entries[0];
		row.loopLengthIfIndependent = 8;
		auto* row_stack = stack.addNoteRow(0, &row);
		row.on_trim = [&] {
			switch (change) {
			case 0:
				currentSong = &other_song;
				break;
			case 1:
				song.owns_clip = false;
				break;
			case 2:
				++row.undo_identity;
				break;
			case 3:
				clip.loopLength = 32;
				break;
			case 4:
				row.loopLengthIfIndependent = 6;
				break;
			case 5:
				row_stack->noteRowId = 1;
				break;
			}
		};
		CHECK_TRUE(row.setLength(row_stack, 4, nullptr, 25, false) == Error::BUG);
		LONGS_EQUAL(-1, row.lastProcessedPosIfIndependent);
		LONGS_EQUAL(0, row.resumed);
	}
}
TEST(ClipRepeat, row_setter_rejects_target_deletion_during_trim) {
	for (bool delete_clip : {false, true}) {
		auto target = std::make_unique<InstrumentClip>();
		target->noteRows.entries.resize(1);
		stack.clip = target.get();
		song.owns_clip = true;
		auto* row = &target->noteRows.entries[0];
		auto* row_stack = stack.addNoteRow(0, row);
		row->on_trim = [&] {
			if (delete_clip) {
				song.owns_clip = false;
				target.reset();
			}
			else {
				target->noteRows.entries.clear();
			}
		};
		CHECK_TRUE(row->setLength(row_stack, 4, nullptr, 25, false) == Error::BUG);
	}
}

TEST(ClipRepeat, row_setter_reports_resume_conflicts_to_direct_callers) {
	Song other_song;
	for (int change = 0; change < 4; ++change) {
		currentSong = &song;
		clip.loopLength = 16;
		auto& row = clip.noteRows.entries[0];
		row.loopLengthIfIndependent = 8;
		auto* row_stack = stack.addNoteRow(0, &row);
		row.on_resume = [&] {
			switch (change) {
			case 0:
				row.loopLengthIfIndependent = 3;
				break;
			case 1:
				row_stack->row = &clip.noteRows.entries[1];
				break;
			case 2:
				currentSong = &other_song;
				break;
			case 3:
				++row.undo_identity;
				break;
			}
		};
		CHECK_TRUE(row.setLength(row_stack, 4, nullptr, 25, false) == Error::BUG);
	}
}
TEST(ClipRepeat, row_setter_rejects_target_destruction_during_resume) {
	for (bool delete_clip : {false, true}) {
		auto target = std::make_unique<InstrumentClip>();
		target->noteRows.entries.resize(1);
		stack.clip = target.get();
		song.owns_clip = true;
		auto* row = &target->noteRows.entries[0];
		auto* row_stack = stack.addNoteRow(0, row);
		row->on_resume = [&] {
			if (delete_clip) {
				song.owns_clip = false;
				target.reset();
			}
			else {
				target->noteRows.entries.clear();
			}
		};
		CHECK_TRUE(row->setLength(row_stack, 4, nullptr, 25, false) == Error::BUG);
	}
}

TEST(ClipRepeat, row_setter_tracks_publication_between_trim_and_resume) {
	auto target = std::make_unique<InstrumentClip>();
	target->noteRows.entries.resize(1);
	stack.clip = target.get();
	song.owns_clip = false;
	auto* row = &target->noteRows.entries[0];
	auto* row_stack = stack.addNoteRow(0, row);
	row->on_trim = [&] { song.owns_clip = true; };
	row->on_resume = [&] {
		song.owns_clip = false;
		target.reset();
	};
	CHECK_TRUE(row->setLength(row_stack, 4, nullptr, 25, false) == Error::BUG);
	CHECK_TRUE(target == nullptr);
}
TEST(ClipRepeat, row_setter_preserves_unpublished_targets_and_successful_publication) {
	for (bool publish_during_trim : {false, true}) {
		song.owns_clip = false;
		auto& row = clip.noteRows.entries[0];
		row.loopLengthIfIndependent = 8;
		auto* row_stack = stack.addNoteRow(0, &row);
		row.on_trim = [&] { song.owns_clip = publish_during_trim; };
		CHECK_TRUE(row.setLength(row_stack, 4, nullptr, 25, false) == Error::NONE);
		LONGS_EQUAL(4, row.loopLengthIfIndependent);
		CHECK_TRUE(song.owns_clip == publish_during_trim);
	}
}

TEST(ClipRepeat, repeat_operations_track_publication_before_later_row_deletion) {
	for (int operation = 0; operation < 3; ++operation) {
		auto target = std::make_unique<InstrumentClip>();
		target->noteRows.entries.resize(2);
		stack.clip = target.get();
		song.owns_clip = false;
		auto publish = [&] { song.owns_clip = true; };
		auto remove = [&] {
			song.owns_clip = false;
			target.reset();
		};
		target->noteRows.entries[0].on_repeat = publish;
		target->noteRows.entries[0].on_trim = publish;
		target->noteRows.entries[1].on_repeat = remove;
		target->noteRows.entries[1].on_trim = remove;
		bool result = operation == 0 ? target->increaseLengthWithRepeats(
		                                   &stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr)
		                             : target->repeatOrChopToExactLength(&stack, operation == 1 ? 32 : 8);
		CHECK_FALSE(result);
		CHECK_TRUE(target == nullptr);
	}
}
TEST(ClipRepeat, repeat_operations_accept_publication_without_later_removal) {
	for (int operation = 0; operation < 3; ++operation) {
		InstrumentClip target;
		target.noteRows.entries.resize(2);
		stack.clip = &target;
		song.owns_clip = false;
		auto publish = [&] { song.owns_clip = true; };
		target.noteRows.entries[0].on_repeat = publish;
		target.noteRows.entries[0].on_trim = publish;
		bool result =
		    operation == 0
		        ? target.increaseLengthWithRepeats(&stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr)
		        : target.repeatOrChopToExactLength(&stack, operation == 1 ? 32 : 8);
		CHECK_TRUE(result);
		CHECK_TRUE(song.owns_clip);
	}
}

TEST(ClipRepeat, song_doubling_rejects_missing_target_or_output) {
	CHECK_FALSE(song.doubleClipLength(nullptr));
	clip.output = nullptr;
	CHECK_FALSE(song.doubleClipLength(&clip));
	untouched();
}
TEST(ClipRepeat, song_doubling_stops_when_callbacks_delete_registered_clip) {
	for (int stage = 0; stage < 3; ++stage) {
		song.owns_clip = true;
		song.scale_changes = 0;
		playback_mode = {};
		auto* target = new InstrumentClip;
		song.syncScalingClip = target;
		auto remove_target = [&] {
			song.owns_clip = false;
			delete target;
		};
		song.on_scale = stage == 0 ? std::function<void()>(remove_target) : nullptr;
		target->storage.on_notify = stage == 1 ? std::function<void()>(remove_target) : nullptr;
		playback_mode.on_resync = stage == 2 ? std::function<void()>(remove_target) : nullptr;
		CHECK_FALSE(song.doubleClipLength(target));
		LONGS_EQUAL(stage == 2 ? 1 : 0, playback_mode.resyncs);
	}
	playback_mode = {};
	song.on_scale = {};
}
TEST(ClipRepeat, song_doubling_rejects_callback_changes_to_context) {
	using namespace deluge::gui::ui_session;
	for (int stage = 0; stage < 3; ++stage) {
		for (int change = 0; change < 6; ++change) {
			InstrumentClip target;
			Song replacement;
			currentSong = &song;
			song.syncScalingClip = &target;
			playback_mode = {};
			auto invalidate = [&] {
				switch (change) {
				case 0:
					currentSong = &replacement;
					break;
				case 1:
					target.output = nullptr;
					break;
				case 2:
					target.loopLength = 7;
					break;
				case 3:
					target.type = ClipType::AUDIO;
					break;
				case 4:
					navigation.for_owner(Id::Local).structural_refresh.request();
					break;
				case 5:
					navigation.for_owner(Id::Remote).structural_refresh.request();
					break;
				}
			};
			song.on_scale = stage == 0 ? std::function<void()>(invalidate) : nullptr;
			target.storage.on_notify = stage == 1 ? std::function<void()>(invalidate) : nullptr;
			playback_mode.on_resync = stage == 2 ? std::function<void()>(invalidate) : nullptr;
			CHECK_FALSE(song.doubleClipLength(&target));
			LONGS_EQUAL(stage >= 1 ? 1 : 0, target.storage.length_changes);
			LONGS_EQUAL(stage == 2 ? 1 : 0, playback_mode.resyncs);
		}
	}
	currentSong = &song;
	song.on_scale = {};
	playback_mode = {};
}

TEST(ClipRepeat, song_doubling_accepts_unpublished_clip_and_restored_nested_owner) {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		Scope initiating(owner);
		InstrumentClip target;
		song.owns_clip = false;
		song.syncScalingClip = &target;
		song.on_scale = [&] { Scope nested(owner == Id::Local ? Id::Remote : Id::Local); };
		CHECK_TRUE(song.doubleClipLength(&target));
		LONGS_EQUAL(32, target.loopLength);
		LONGS_EQUAL(1, target.storage.length_changes);
	}
	song.on_scale = {};
}

TEST(ClipRepeat, repeat_operations_reject_missing_output_and_wrong_type_before_rows) {
	for (bool missing_output : {false, true}) {
		InstrumentClip target;
		target.noteRows.entries.resize(2);
		ModelStackWithTimelineCounter target_stack{&song, &target};
		if (missing_output)
			target.output = nullptr;
		else
			target.type = ClipType::AUDIO;
		CHECK_FALSE(target.increaseLengthWithRepeats(&target_stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false,
		                                             nullptr));
		CHECK_FALSE(target.repeatOrChopToExactLength(&target_stack, 32));
		CHECK_FALSE(target.repeatOrChopToExactLength(&target_stack, 8));
		LONGS_EQUAL(0, target.noteRows.entries[0].repeats);
		LONGS_EQUAL(0, target.noteRows.entries[0].trims);
		LONGS_EQUAL(0, target.paramManager.repeats);
		LONGS_EQUAL(16, target.loopLength);
	}
}
TEST(ClipRepeat, repeat_operations_stop_on_changed_direction_type_or_ui_owner) {
	using namespace deluge::gui::ui_session;
	for (int operation = 0; operation < 3; ++operation) {
		for (int stage = 0; stage < (operation == 0 ? 2 : 4); ++stage) {
			if (operation == 2 && stage == 1)
				continue; // Chopping does not generate parameter repeats.
			for (int change = 0; change < 3; ++change) {
				Scope initiating(Id::Local);
				std::optional<Scope> switched_owner;
				InstrumentClip target;
				target.noteRows.entries.resize(2);
				ModelStackWithTimelineCounter target_stack{&song, &target};
				auto invalidate = [&] {
					if (change == 0)
						target.sequenceDirectionMode = SequenceDirection::REVERSE;
					if (change == 1)
						target.type = ClipType::AUDIO;
					if (change == 2)
						switched_owner.emplace(Id::Remote);
				};
				if (stage == 0) {
					target.noteRows.entries[0].on_repeat = invalidate;
					target.noteRows.entries[0].on_trim = invalidate;
				}
				if (stage == 1)
					target.paramManager.on_repeat = invalidate;
				if (stage == 2)
					target.on_length_changed = invalidate;
				if (stage == 3)
					target.on_resume = invalidate;
				const bool result =
				    operation == 0 ? target.increaseLengthWithRepeats(
				                         &target_stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr)
				                   : target.repeatOrChopToExactLength(&target_stack, operation == 1 ? 32 : 8);
				CHECK_FALSE(result);
				if (stage == 0) {
					LONGS_EQUAL(0, target.noteRows.entries[1].repeats);
					LONGS_EQUAL(0, target.noteRows.entries[1].trims);
					LONGS_EQUAL(0, target.paramManager.repeats);
				}
				LONGS_EQUAL(stage >= 2 ? 1 : 0, target.changed);
				LONGS_EQUAL(stage == 3 ? 1 : 0, target.resumed);
				if (change == 0)
					CHECK_TRUE(target.sequenceDirectionMode == SequenceDirection::REVERSE);
			}
		}
	}
}
TEST(ClipRepeat, exact_repeat_accepts_nested_owners_after_direction_flattening) {
	using namespace deluge::gui::ui_session;
	for (Id owner : {Id::Local, Id::Remote}) {
		Scope initiating(owner);
		InstrumentClip target;
		target.noteRows.entries.resize(2);
		ModelStackWithTimelineCounter target_stack{&song, &target};
		auto nested = [&] { Scope other(owner == Id::Local ? Id::Remote : Id::Local); };
		target.noteRows.entries[0].on_repeat = nested;
		target.paramManager.on_repeat = nested;
		target.on_length_changed = nested;
		target.on_resume = nested;
		CHECK_TRUE(target.repeatOrChopToExactLength(&target_stack, 32));
		CHECK_TRUE(target.sequenceDirectionMode == SequenceDirection::FORWARD);
		LONGS_EQUAL(32, target.loopLength);
		LONGS_EQUAL(1, target.resumed);
	}
}

TEST(ClipRepeat, repeat_and_chop_reject_redirected_row_stack_before_following_work) {
	for (int operation = 0; operation < 3; ++operation) {
		for (int field = 0; field < 4; ++field) {
			InstrumentClip target, replacement_clip;
			Song replacement_song;
			NoteRow replacement_row;
			target.noteRows.entries.resize(2);
			target.noteRows.entries[0].loopLengthIfIndependent = 12;
			ModelStackWithTimelineCounter target_stack{&song, &target};
			auto redirect = [&] {
				switch (field) {
				case 0:
					target_stack.row.song = &replacement_song;
					break;
				case 1:
					target_stack.row.clip = &replacement_clip;
					break;
				case 2:
					target_stack.row.row = &replacement_row;
					break;
				case 3:
					++target_stack.row.noteRowId;
					break;
				}
			};
			target.noteRows.entries[0].on_repeat = redirect;
			target.noteRows.entries[0].on_trim = redirect;
			const bool success = operation == 0
			                         ? target.increaseLengthWithRepeats(
			                               &target_stack, 32, IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr)
			                         : target.repeatOrChopToExactLength(&target_stack, operation == 1 ? 32 : 8);
			CHECK_FALSE(success);
			LONGS_EQUAL(12, target.noteRows.entries[0].loopLengthIfIndependent);
			LONGS_EQUAL(0, target.noteRows.entries[1].repeats);
			LONGS_EQUAL(0, target.noteRows.entries[1].trims);
			LONGS_EQUAL(0, target.paramManager.repeats);
			LONGS_EQUAL(16, target.loopLength);
			LONGS_EQUAL(0, target.changed);
			LONGS_EQUAL(0, target.resumed);
		}
	}
}
TEST(ClipRepeat, repeat_and_chop_accept_row_stack_restored_by_callback) {
	for (int operation = 0; operation < 3; ++operation) {
		InstrumentClip target;
		target.noteRows.entries.resize(2);
		target.noteRows.entries[0].loopLengthIfIndependent = 12;
		ModelStackWithTimelineCounter target_stack{&song, &target};
		auto nested = [&] {
			const auto saved_stack = target_stack.row;
			target_stack.row = {};
			target_stack.row = saved_stack;
		};
		target.noteRows.entries[0].on_repeat = nested;
		target.noteRows.entries[0].on_trim = nested;
		const bool success =
		    operation == 0 ? target.increaseLengthWithRepeats(&target_stack, 32,
		                                                      IndependentNoteRowLengthIncrease::DOUBLE, false, nullptr)
		                   : target.repeatOrChopToExactLength(&target_stack, operation == 1 ? 32 : 8);
		CHECK_TRUE(success);
		LONGS_EQUAL(operation == 0 ? 24 : 0, target.noteRows.entries[0].loopLengthIfIndependent);
		LONGS_EQUAL(operation == 2 ? 8 : 32, target.loopLength);
	}
}
