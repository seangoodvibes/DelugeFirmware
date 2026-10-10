#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include <array>
namespace recording_clone_test {
enum class RecordingMode { OFF, ARRANGEMENT };
constexpr int LESS = -1;
struct Clip;
struct ModelStackWithTimelineCounter;
struct ClipInstance {
	Clip* clip = nullptr;
	int32_t pos = 0;
	int32_t length = 64;
};
struct instances_fixture {
	std::array<ClipInstance, 2> values;
	int search_result = 0;
	Error insert_error = Error::NONE;
	int inserts = 0;
	int search(int, int) { return search_result; }
	ClipInstance* getElement(int index) { return &values[index]; }
	Error insertAtIndex(int) {
		++inserts;
		return insert_error;
	}
};
struct Output {
	Clip* active = nullptr;
	instances_fixture clipInstances;
	Clip* getActiveClip() { return active; }
	void setActiveClip(ModelStackWithTimelineCounter*, PgmChangeSend);
};
struct song_fixture {
	bool active = true;
	bool isClipActive(Clip*) { return active; }
	struct {
		bool reserve_ok = true;
		int inserts = 0;
		Clip* inserted = nullptr;
		bool ensureEnoughSpaceAllocated(int) { return reserve_ok; }
		void insertClipAtIndex(Clip* clip, int) {
			++inserts;
			inserted = clip;
		}
	} arrangementOnlyClips;
};
struct ModelStackWithTimelineCounter {
	song_fixture* song = nullptr;
	Clip* clip = nullptr;
	Clip* getTimelineCounter() { return clip; }
	void setTimelineCounter(Clip* target) { clip = target; }
};
struct Clip {
	Output* output = nullptr;
	Clip* beingRecordedFromClip = nullptr;
	Clip* clone_target = nullptr;
	Error clone_error = Error::NONE;
	ClipType type = ClipType::INSTRUMENT;
	bool arrangement_only = false;
	bool activeIfNoSolo = false;
	bool currentlyPlayingReversed = false;
	int32_t repeatCount = 0, loopLength = 64, lastProcessedPos = 5;
	int section = 0;
	int clone_calls = 0, stop_calls = 0, resume_calls = 0;
	int new_position = -1, new_length = -1;
	bool isArrangementOnlyClip() { return arrangement_only; }
	Error clone(ModelStackWithTimelineCounter* stack, bool) {
		++clone_calls;
		if (clone_error == Error::NONE)
			stack->clip = clone_target;
		return clone_error;
	}
	void increaseLengthWithRepeats(ModelStackWithTimelineCounter*, int length, IndependentNoteRowLengthIncrease, bool) {
		new_length = length;
	}
	void expectNoFurtherTicks(song_fixture*, bool) { ++stop_calls; }
	void setPos(ModelStackWithTimelineCounter*, int position, bool) { new_position = position; }
	void resumePlayback(ModelStackWithTimelineCounter*, bool) { ++resume_calls; }
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter*, Error* = nullptr);
};
struct AudioClip : Clip {
	void* voiceSample = nullptr;
};
void Output::setActiveClip(ModelStackWithTimelineCounter* stack, PgmChangeSend) {
	active = stack->clip;
}
static struct {
	RecordingMode recording = RecordingMode::ARRANGEMENT;
	bool clock = true;
	bool isEitherClockActive() { return clock; }
	int getActualArrangementRecordPos() { return 0; }
} playbackHandler;
#include "recording_clone.inc"
} // namespace recording_clone_test
using namespace recording_clone_test;
TEST_GROUP(RecordingClone) {
	AudioClip original, cloned;
	Output output;
	song_fixture song;
	ModelStackWithTimelineCounter stack;
	Error result = Error::BUG;
	void setup() override {
		original.output = cloned.output = &output;
		original.clone_target = &cloned;
		output.active = &original;
		output.clipInstances.values[0].clip = &original;
		stack.song = &song;
		stack.clip = &original;
		playbackHandler = {};
	}
	bool attempt_clone() {
		return original.possiblyCloneForArrangementRecording(&stack, &result);
	}
};
TEST(RecordingClone, unnecessary_clone_clears_error_without_modifying_original) {
	original.arrangement_only = true;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::NONE);
	LONGS_EQUAL(0, original.clone_calls);
	POINTERS_EQUAL(&original, stack.clip);
}
TEST(RecordingClone, allocation_and_missing_instance_failures_are_reported) {
	song.arrangementOnlyClips.reserve_ok = false;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::INSUFFICIENT_RAM);
	song.arrangementOnlyClips.reserve_ok = true;
	output.clipInstances.search_result = -1;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	LONGS_EQUAL(0, original.clone_calls);
	POINTERS_EQUAL(&original, stack.clip);
	POINTERS_EQUAL(&original, output.active);
}
TEST(RecordingClone, instance_insertion_and_clone_failures_are_reported) {
	original.type = ClipType::AUDIO;
	original.repeatCount = 1;
	output.clipInstances.insert_error = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(0, original.clone_calls);
	original.repeatCount = 0;
	original.clone_error = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::INSUFFICIENT_RAM);
	POINTERS_EQUAL(&original, stack.clip);
	POINTERS_EQUAL(&original, output.active);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(0, original.stop_calls);
}
TEST(RecordingClone, successful_clone_and_existing_recording_target_report_change) {
	CHECK(attempt_clone());
	CHECK(result == Error::NONE);
	POINTERS_EQUAL(&cloned, stack.clip);
	POINTERS_EQUAL(&cloned, output.active);
	POINTERS_EQUAL(&original, cloned.beingRecordedFromClip);
	LONGS_EQUAL(1, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(1, cloned.resume_calls);
	stack.clip = &original;
	result = Error::BUG;
	CHECK(attempt_clone());
	CHECK(result == Error::NONE);
	POINTERS_EQUAL(&cloned, stack.clip);
	LONGS_EQUAL(1, original.clone_calls);
}
TEST(RecordingClone, missing_context_reports_error_and_legacy_call_still_works) {
	CHECK_FALSE(original.possiblyCloneForArrangementRecording(nullptr, &result));
	CHECK(result == Error::BUG);
	stack.song = nullptr;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	stack.song = &song;
	original.output = nullptr;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	original.output = &output;
	original.arrangement_only = true;
	CHECK_FALSE(original.possiblyCloneForArrangementRecording(&stack));
}
