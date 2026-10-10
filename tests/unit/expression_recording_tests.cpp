#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "util/lifetime.h"
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <type_traits>
namespace expression_recording_test {
static std::function<void()> on_clone, on_record;

template <int shift>
static int32_t lshiftAndSaturate(int32_t value) {
	return std::clamp<int64_t>(int64_t(value) * (int64_t(1) << shift), INT32_MIN, INT32_MAX);
}
static bool expressionValueChangesMustBeDoneSmoothly = false;
enum class RecordingMode { OFF, ARRANGEMENT };
static struct {
	bool clock = true;
	RecordingMode recording = RecordingMode::ARRANGEMENT;
	bool isEitherClockActive() { return clock; }
} playbackHandler;
struct ModelStackWithNoteRow;
struct NoteRow {
	int records = 0;
	bool success = true;
	bool recordPolyphonicExpressionEvent(ModelStackWithNoteRow*, int32_t, int32_t, bool) {
		++records;
		auto callback = on_record;
		if (callback)
			callback();
		return success;
	}
};
struct ModelStackWithNoteRow {
	NoteRow* row = nullptr;
	NoteRow* getNoteRowAllowNull() { return row; }
};
struct InstrumentClip;
struct ModelStackWithTimelineCounter {
	InstrumentClip* timeline = nullptr;
	bool timelineCounterIsSet() { return timeline != nullptr; }
	InstrumentClip* getTimelineCounter() { return timeline; }
};
struct InstrumentClip {
	Error clone_error = Error::NONE;
	InstrumentClip* clone_target = nullptr;
	int clones = 0, row_lookups = 0;
	ModelStackWithNoteRow row_stack;
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* stack, Error* error) {
		++clones;
		*error = clone_error;
		auto callback = on_clone;
		if (callback)
			callback();
		if (clone_error == Error::NONE && clone_target) {
			stack->timeline = clone_target;
			return true;
		}
		return false;
	}
	ModelStackWithNoteRow* getNoteRowForDrum(ModelStackWithTimelineCounter*, void*) {
		++row_lookups;
		return &row_stack;
	}
	ModelStackWithNoteRow* getNoteRowForYNote(int, ModelStackWithTimelineCounter*) {
		++row_lookups;
		return &row_stack;
	}
};
struct live_fixture {
	int sends = 0;
	bool smooth_during_send = false;
	void send() {
		++sends;
		smooth_during_send = expressionValueChangesMustBeDoneSmoothly;
	}
};
struct Drum : live_fixture {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	int16_t lastExpressionInputsReceived[2][3]{};
	void expressionEvent(int32_t, int32_t) { send(); }
	void expressionEventPossiblyToRecord(ModelStackWithTimelineCounter*, int16_t, int32_t, int32_t);
};
struct ArpNote {
	int inputCharacteristics[2]{};
};
struct MelodicInstrument : live_fixture {
	struct {
		struct {
			std::array<ArpNote, 2> values;
			int getNumElements() { return values.size(); }
			void* getElementAddress(int index) { return &values[index]; }
		} notes;
	} arpeggiator;
	void polyphonicExpressionEventOnChannelOrNote(int32_t, int32_t, int32_t, MIDICharacteristic) { send(); }
	void polyphonicExpressionEventPossiblyToRecord(ModelStackWithTimelineCounter*, int32_t, int32_t, int32_t,
	                                               MIDICharacteristic);
};
#include "drum_expression_recording.inc"
#include "melodic_expression_recording.inc"
} // namespace expression_recording_test
using namespace expression_recording_test;
TEST_GROUP(ExpressionRecording) {
	InstrumentClip original, cloned;
	NoteRow original_row, cloned_row;
	ModelStackWithTimelineCounter stack;
	Drum drum;
	MelodicInstrument melodic;
	void setup() override {
		on_clone = on_record = {};
		playbackHandler = {};
		expressionValueChangesMustBeDoneSmoothly = false;
		original.row_stack.row = &original_row;
		cloned.row_stack.row = &cloned_row;
		stack.timeline = &original;
		for (auto& note : melodic.arpeggiator.notes.values) {
			note.inputCharacteristics[util::to_underlying(MIDICharacteristic::CHANNEL)] = 1;
			note.inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)] = 60;
		}
	}
	void teardown() override {
		on_clone = on_record = {};
	}
	void send(bool is_drum, ModelStackWithTimelineCounter* target) {
		if (is_drum)
			drum.expressionEventPossiblyToRecord(target, 256, 0, 0);
		else
			melodic.polyphonicExpressionEventPossiblyToRecord(target, 256, 0, 1, MIDICharacteristic::CHANNEL);
	}
};
TEST(ExpressionRecording, failed_clone_sounds_expression_without_recording_original) {
	original.clone_error = Error::INSUFFICIENT_RAM;
	send(true, &stack);
	send(false, &stack);
	LONGS_EQUAL(0, original.row_lookups);
	LONGS_EQUAL(0, original_row.records);
	LONGS_EQUAL(1, drum.sends);
	LONGS_EQUAL(1, melodic.sends);
	CHECK(drum.smooth_during_send);
	CHECK(melodic.smooth_during_send);
	CHECK_FALSE(expressionValueChangesMustBeDoneSmoothly);
	POINTERS_EQUAL(&original, stack.timeline);
}
TEST(ExpressionRecording, successful_clone_records_only_resulting_clip_rows) {
	original.clone_target = &cloned;
	send(true, &stack);
	stack.timeline = &original;
	send(false, &stack);
	LONGS_EQUAL(0, original_row.records);
	LONGS_EQUAL(3, cloned_row.records);
	LONGS_EQUAL(0, drum.sends);
	LONGS_EQUAL(0, melodic.sends);
	CHECK_FALSE(expressionValueChangesMustBeDoneSmoothly);
}
TEST(ExpressionRecording, unavailable_timeline_preserves_live_expression) {
	for (bool is_drum : {false, true}) {
		send(is_drum, nullptr);
		stack.timeline = nullptr;
		send(is_drum, &stack);
	}
	LONGS_EQUAL(2, drum.sends);
	LONGS_EQUAL(2, melodic.sends);
	LONGS_EQUAL(0, original.clones);
	CHECK_FALSE(expressionValueChangesMustBeDoneSmoothly);
}
TEST(ExpressionRecording, row_recording_failure_preserves_existing_live_fallback) {
	original_row.success = false;
	send(true, &stack);
	send(false, &stack);
	LONGS_EQUAL(3, original_row.records);
	LONGS_EQUAL(1, drum.sends);
	LONGS_EQUAL(2, melodic.sends);
	CHECK_FALSE(expressionValueChangesMustBeDoneSmoothly);
}

TEST(ExpressionRecording, drum_destroyed_during_clone_is_not_used_for_recording_or_fallback) {
	for (Error clone_error : {Error::NONE, Error::INSUFFICIENT_RAM}) {
		auto target = std::make_unique<Drum>();
		original.clone_error = clone_error;
		on_clone = [&] { target.reset(); };
		target->expressionEventPossiblyToRecord(&stack, 256, 0, 0);
		LONGS_EQUAL(0, original.row_lookups);
		LONGS_EQUAL(0, original_row.records);
		CHECK_FALSE(expressionValueChangesMustBeDoneSmoothly);
	}
}
TEST(ExpressionRecording, drum_address_reuse_during_clone_does_not_send_to_replacement) {
	auto* target = new Drum;
	original.clone_error = Error::INSUFFICIENT_RAM;
	on_clone = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
	};
	target->expressionEventPossiblyToRecord(&stack, 256, 0, 0);
	LONGS_EQUAL(0, target->sends);
	LONGS_EQUAL(0, original_row.records);
	CHECK_FALSE(expressionValueChangesMustBeDoneSmoothly);
	delete target;
}
TEST(ExpressionRecording, drum_destroyed_during_recording_is_not_used_for_fallback) {
	auto target = std::make_unique<Drum>();
	original_row.success = false;
	on_record = [&] { target.reset(); };
	target->expressionEventPossiblyToRecord(&stack, 256, 0, 0);
	LONGS_EQUAL(1, original_row.records);
	CHECK_FALSE(expressionValueChangesMustBeDoneSmoothly);
}
TEST(ExpressionRecording, retiring_drum_rejects_expression_without_changing_input_or_smoothing) {
	drum.lifetime_source.retire();
	expressionValueChangesMustBeDoneSmoothly = true;
	drum.expressionEventPossiblyToRecord(&stack, 256, 0, 0);
	LONGS_EQUAL(0, original.clones);
	LONGS_EQUAL(0, drum.sends);
	LONGS_EQUAL(0, drum.lastExpressionInputsReceived[0][0]);
	CHECK(expressionValueChangesMustBeDoneSmoothly);
	expressionValueChangesMustBeDoneSmoothly = false;
}
TEST(ExpressionRecording, nested_drum_expression_restores_outer_smoothing_state) {
	Drum nested;
	on_clone = [&] {
		CHECK(expressionValueChangesMustBeDoneSmoothly);
		nested.expressionEventPossiblyToRecord(nullptr, 256, 0, 0);
		CHECK(expressionValueChangesMustBeDoneSmoothly);
	};
	drum.expressionEventPossiblyToRecord(&stack, 256, 0, 0);
	LONGS_EQUAL(1, nested.sends);
	CHECK(nested.smooth_during_send);
	CHECK_FALSE(expressionValueChangesMustBeDoneSmoothly);
}
