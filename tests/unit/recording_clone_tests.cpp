#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_navigation_state.h"
#include <array>
#include <functional>
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
	std::function<void()> on_insert;
	int search(int, int) { return search_result; }
	ClipInstance* getElement(int index) { return &values[index]; }
	Error insertAtIndex(int) {
		++inserts;
		if (on_insert)
			on_insert();
		return insert_error;
	}
};
struct Output {
	Clip* active = nullptr;
	std::function<void()> on_activate;
	instances_fixture clipInstances;
	Clip* getActiveClip() { return active; }
	bool clipHasInstance(Clip* clip) {
		for (auto& instance : clipInstances.values)
			if (instance.clip == clip)
				return true;
		return false;
	}
	void setActiveClip(ModelStackWithTimelineCounter*, PgmChangeSend);
};
struct song_fixture {
	int deletions = 0;
	Clip* deleted_clip = nullptr;
	void deleteClipObject(Clip* clip, bool, InstrumentRemoval) {
		++deletions;
		deleted_clip = clip;
	}
	Clip* owned_clip = nullptr;
	bool contains_clip_for_undo(Clip* clip) { return clip == owned_clip || clip == arrangementOnlyClips.inserted; }
	bool active = true;
	bool isClipActive(Clip*) { return active; }
	struct {
		bool reserve_ok = true;
		std::function<void()> on_reserve;
		int inserts = 0;
		Error insert_error = Error::NONE;
		Clip* inserted = nullptr;
		bool ensureEnoughSpaceAllocated(int) {
			if (on_reserve)
				on_reserve();
			return reserve_ok;
		}
		Error insertClipAtIndex(Clip* clip, int) {
			++inserts;
			if (insert_error == Error::NONE)
				inserted = clip;
			return insert_error;
		}
	} arrangementOnlyClips;
};
static song_fixture* currentSong = nullptr;
struct ModelStackWithTimelineCounter {
	song_fixture* song = nullptr;
	Clip* clip = nullptr;
	Clip* getTimelineCounter() { return clip; }
	Clip* getTimelineCounterAllowNull() { return clip; }
	void setTimelineCounter(Clip* target) { clip = target; }
};
struct Clip {
	Output* output = nullptr;
	Clip* beingRecordedFromClip = nullptr;
	Clip* clone_target = nullptr;
	Error clone_error = Error::NONE;
	std::function<void(ModelStackWithTimelineCounter*)> on_clone;
	ClipType type = ClipType::INSTRUMENT;
	bool arrangement_only = false;
	bool activeIfNoSolo = false;
	bool currentlyPlayingReversed = false;
	int32_t repeatCount = 0, loopLength = 64, lastProcessedPos = 5;
	int section = 0;
	int clone_calls = 0, stop_calls = 0, resume_calls = 0;
	std::function<void()> on_stop, on_position, on_resume;
	int new_position = -1, new_length = -1;
	bool isArrangementOnlyClip() { return arrangement_only; }
	Error clone(ModelStackWithTimelineCounter* stack, bool) {
		++clone_calls;
		if (clone_error == Error::NONE)
			stack->clip = clone_target;
		if (on_clone)
			on_clone(stack);
		return clone_error;
	}
	bool repeat_success = true;
	std::function<void()> on_repeat;
	bool increaseLengthWithRepeats(ModelStackWithTimelineCounter*, int length, IndependentNoteRowLengthIncrease, bool) {
		new_length = length;
		if (on_repeat)
			on_repeat();
		return repeat_success;
	}
	void expectNoFurtherTicks(song_fixture*, bool) {
		++stop_calls;
		if (on_stop)
			on_stop();
	}
	void setPos(ModelStackWithTimelineCounter*, int position, bool) {
		new_position = position;
		if (on_position)
			on_position();
	}
	void resumePlayback(ModelStackWithTimelineCounter*, bool) {
		++resume_calls;
		if (on_resume)
			on_resume();
	}
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter*, Error* = nullptr);
};
struct AudioClip : Clip {
	void* voiceSample = nullptr;
};
void Output::setActiveClip(ModelStackWithTimelineCounter* stack, PgmChangeSend) {
	active = stack->clip;
	if (on_activate)
		on_activate();
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
		deluge::gui::ui_session::navigation = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
		original.output = cloned.output = &output;
		original.clone_target = &cloned;
		output.active = &original;
		output.clipInstances.values[0].clip = &original;
		stack.song = &song;
		currentSong = &song;
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

TEST(RecordingClone, insertion_failure_preserves_original_instance_extent) {
	original.type = ClipType::AUDIO;
	original.repeatCount = 2;
	output.clipInstances.values[0].pos = 11;
	output.clipInstances.values[0].length = 256;
	output.clipInstances.insert_error = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(attempt_clone());
	LONGS_EQUAL(256, output.clipInstances.values[0].length);
	LONGS_EQUAL(11, output.clipInstances.values[0].pos);
	POINTERS_EQUAL(&original, output.clipInstances.values[0].clip);
	LONGS_EQUAL(0, original.clone_calls);
}

TEST(RecordingClone, successful_repeated_audio_clone_keeps_prior_repeats) {
	original.type = cloned.type = ClipType::AUDIO;
	original.repeatCount = 2;
	output.clipInstances.values[0].pos = 11;
	output.clipInstances.values[0].length = 256;
	int sample;
	original.voiceSample = &sample;
	CHECK(attempt_clone());
	LONGS_EQUAL(128, output.clipInstances.values[0].length);
	POINTERS_EQUAL(&original, output.clipInstances.values[0].clip);
	LONGS_EQUAL(139, output.clipInstances.values[1].pos);
	LONGS_EQUAL(64, output.clipInstances.values[1].length);
	POINTERS_EQUAL(&cloned, output.clipInstances.values[1].clip);
	POINTERS_EQUAL(&sample, cloned.voiceSample);
	POINTERS_EQUAL(nullptr, original.voiceSample);
	CHECK(result == Error::NONE);
}

TEST(RecordingClone, invalid_success_result_never_repurposes_original_or_owned_clip) {
	for (auto* target : {static_cast<Clip*>(nullptr), static_cast<Clip*>(&original), static_cast<Clip*>(&cloned)}) {
		original.clone_target = target;
		song.owned_clip = &cloned;
		CHECK_FALSE(attempt_clone());
		CHECK(result == Error::BUG);
		LONGS_EQUAL(0, original.section);
		LONGS_EQUAL(0, cloned.section);
		LONGS_EQUAL(0, original.stop_calls);
		LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
		POINTERS_EQUAL(&original, output.clipInstances.values[0].clip);
	}
}
TEST(RecordingClone, changed_song_during_clone_stops_publication) {
	song_fixture replacement;
	for (bool change_current : {false, true}) {
		currentSong = &song;
		stack.song = &song;
		stack.clip = &original;
		original.on_clone = [&](ModelStackWithTimelineCounter* model_stack) {
			if (change_current)
				currentSong = &replacement;
			else
				model_stack->song = &replacement;
		};
		CHECK_FALSE(attempt_clone());
		CHECK(result == Error::BUG);
		LONGS_EQUAL(0, cloned.section);
		LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
		LONGS_EQUAL(0, original.stop_calls);
	}
}

TEST(RecordingClone, repeat_failure_discards_only_unpublished_clone) {
	cloned.repeat_success = false;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	LONGS_EQUAL(1, song.deletions);
	POINTERS_EQUAL(&cloned, song.deleted_clip);
	POINTERS_EQUAL(&original, stack.clip);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(0, original.stop_calls);
	POINTERS_EQUAL(&original, output.clipInstances.values[0].clip);
}
TEST(RecordingClone, publication_failure_discards_unpublished_clone_and_reports_error) {
	song.arrangementOnlyClips.insert_error = Error::INSUFFICIENT_RAM;
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::INSUFFICIENT_RAM);
	LONGS_EQUAL(1, song.deletions);
	POINTERS_EQUAL(&cloned, song.deleted_clip);
	POINTERS_EQUAL(&original, stack.clip);
	LONGS_EQUAL(0, original.stop_calls);
	POINTERS_EQUAL(&original, output.clipInstances.values[0].clip);
}
TEST(RecordingClone, repeat_callback_adoption_prevents_clone_destruction) {
	for (int adoption = 0; adoption < 3; ++adoption) {
		song.owned_clip = nullptr;
		output.active = &original;
		output.clipInstances.values[1].clip = nullptr;
		cloned.repeat_success = false;
		cloned.on_repeat = [&] {
			if (adoption == 0)
				song.owned_clip = &cloned;
			if (adoption == 1)
				output.active = &cloned;
			if (adoption == 2)
				output.clipInstances.values[1].clip = &cloned;
		};
		CHECK_FALSE(attempt_clone());
		LONGS_EQUAL(0, song.deletions);
		LONGS_EQUAL(0, original.stop_calls);
	}
}
TEST(RecordingClone, changed_structural_revision_prevents_unsafe_cleanup) {
	namespace panels = deluge::gui::ui_session;
	for (auto owner : {panels::Id::Local, panels::Id::Remote}) {
		cloned.on_repeat = [owner] { panels::navigation.for_owner(owner).structural_refresh.request(); };
		CHECK_FALSE(attempt_clone());
		CHECK(result == Error::BUG);
		LONGS_EQUAL(0, song.deletions);
		LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
		LONGS_EQUAL(0, original.stop_calls);
	}
}

TEST(RecordingClone, successful_repeat_callback_adoption_prevents_duplicate_publication) {
	for (int adoption = 0; adoption < 3; ++adoption) {
		song.owned_clip = nullptr;
		output.active = &original;
		output.clipInstances.values[1].clip = nullptr;
		cloned.on_repeat = [&] {
			if (adoption == 0)
				song.owned_clip = &cloned;
			if (adoption == 1)
				output.active = &cloned;
			if (adoption == 2)
				output.clipInstances.values[1].clip = &cloned;
		};
		CHECK_FALSE(attempt_clone());
		CHECK(result == Error::BUG);
		LONGS_EQUAL(0, song.deletions);
		LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
		LONGS_EQUAL(0, original.stop_calls);
	}
}

TEST(RecordingClone, allocation_callback_invalidation_stops_before_clone) {
	song.arrangementOnlyClips.on_reserve = [&] { currentSong = nullptr; };
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, original.stop_calls);
}
TEST(RecordingClone, audio_insertion_callback_invalidation_prevents_followup) {
	original.type = ClipType::AUDIO;
	original.repeatCount = 2;
	output.clipInstances.values[0].length = 256;
	output.clipInstances.on_insert = [&] { currentSong = nullptr; };
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	LONGS_EQUAL(256, output.clipInstances.values[0].length);
	LONGS_EQUAL(0, original.clone_calls);
}
TEST(RecordingClone, stop_callback_cancels_before_instance_replacement) {
	original.on_stop = [&] { currentSong = nullptr; };
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	LONGS_EQUAL(1, song.arrangementOnlyClips.inserts);
	POINTERS_EQUAL(&original, output.clipInstances.values[0].clip);
	LONGS_EQUAL(-1, cloned.new_position);
	LONGS_EQUAL(0, cloned.resume_calls);
	LONGS_EQUAL(0, song.deletions);
}
TEST(RecordingClone, position_callback_cancels_before_resume) {
	cloned.on_position = [&] { currentSong = nullptr; };
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	LONGS_EQUAL(0, cloned.resume_calls);
	POINTERS_EQUAL(&original, output.active);
}
TEST(RecordingClone, resume_callback_cancels_before_activation_and_restores_owner) {
	namespace panels = deluge::gui::ui_session;
	cloned.on_resume = [&] { panels::detail::active = panels::Id::Remote; };
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	POINTERS_EQUAL(&original, output.active);
	CHECK(panels::current() == panels::Id::Local);
}
TEST(RecordingClone, activation_callback_invalidation_reports_failure_without_disposal) {
	output.on_activate = [&] { currentSong = nullptr; };
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	POINTERS_EQUAL(&cloned, output.active);
	LONGS_EQUAL(0, song.deletions);
}
TEST(RecordingClone, removed_published_clone_stops_before_later_access) {
	original.on_stop = [&] { song.arrangementOnlyClips.inserted = nullptr; };
	CHECK_FALSE(attempt_clone());
	CHECK(result == Error::BUG);
	LONGS_EQUAL(-1, cloned.new_position);
	LONGS_EQUAL(0, cloned.resume_calls);
	LONGS_EQUAL(0, song.deletions);
}
