#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <memory>
#include <vector>
namespace audio_recording_rollover_test {
namespace panels = deluge::gui::ui_session;
enum class RecordingMode { OFF, ARRANGEMENT };
constexpr int LESS = -1;
struct Clip;
struct Output;
struct Song;
struct ModelStackWithTimelineCounter {
	Song* song = nullptr;
	Clip* clip = nullptr;
	Clip* getTimelineCounterAllowNull() { return clip; }
	void setTimelineCounter(Clip* value) { clip = value; }
};
static std::function<void()> on_base;
struct Clip {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	virtual ~Clip() = default;
	Output* output = nullptr;
	Clip* beingRecordedFromClip = nullptr;
	Clip* clone_target = nullptr;
	Error clone_error = Error::NONE;
	int clone_calls = 0, position_calls = 0;
	int32_t loopLength = 64;
	int section = 0;
	bool activeIfNoSolo = true;
	std::function<void()> on_clone, on_position;
	struct parameters {
		int copies = 0;
		void copyOverridingFrom(parameters*) { ++copies; }
	};
	struct {
		parameters values;
		bool present = true;
		parameters* getUnpatchedParamSet() { return present ? &values : nullptr; }
	} paramManager;
	void posReachedEnd(ModelStackWithTimelineCounter*) {
		if (on_base)
			on_base();
	}
	Error clone(ModelStackWithTimelineCounter* stack) {
		++clone_calls;
		const auto result = clone_error;
		auto callback = on_clone;
		if (result == Error::NONE)
			stack->clip = clone_target;
		if (callback)
			callback();
		return result;
	}
	void setPos(ModelStackWithTimelineCounter*, int, bool) {
		++position_calls;
		auto callback = on_position;
		if (callback)
			callback();
	}
};
struct AudioClip : Clip {
	bool arrangement_only = true;
	bool isArrangementOnlyClip() { return arrangement_only; }
	void posReachedEnd(ModelStackWithTimelineCounter*);
};
struct ClipInstance {
	Clip* clip = nullptr;
	int32_t pos = 0, length = 128;
};
struct instances_fixture {
	std::vector<ClipInstance> values = std::vector<ClipInstance>(1);
	bool reserve_ok = true;
	Error insert_error = Error::NONE;
	int search_result = 0, inserts = 0;
	std::function<void()> on_reserve;
	bool ensureEnoughSpaceAllocated(int) {
		const bool result = reserve_ok;
		auto callback = on_reserve;
		if (callback)
			callback();
		return result;
	}
	int search(int, int) { return search_result; }
	int getNumElements() { return values.size(); }
	ClipInstance* getElement(int index) { return &values.at(index); }
	Error insert_at_index_without_allocation(int index) {
		++inserts;
		if (insert_error == Error::NONE)
			values.insert(values.begin() + index, ClipInstance{});
		return insert_error;
	}
	void relocate() {
		auto replacement = values;
		values.swap(replacement);
	}
};
struct Output {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	Clip* active = nullptr;
	instances_fixture clipInstances;
	std::function<void()> on_activate;
	int activations = 0;
	Clip* getActiveClip() { return active; }
	bool clipHasInstance(Clip* clip) {
		for (auto& instance : clipInstances.values)
			if (instance.clip == clip)
				return true;
		return false;
	}
	void setActiveClip(ModelStackWithTimelineCounter* stack, PgmChangeSend) {
		++activations;
		active = stack->clip;
		auto callback = on_activate;
		if (callback)
			callback();
	}
};
struct Song {
	struct {
		std::vector<Clip*> values;
		bool reserve_ok = true;
		Error insert_error = Error::NONE;
		int inserts = 0, removals = 0;
		std::function<void()> on_reserve;
		bool ensureEnoughSpaceAllocated(int) {
			const bool result = reserve_ok;
			auto callback = on_reserve;
			if (callback)
				callback();
			return result;
		}
		Error insert_at_index_without_allocation(int index) {
			++inserts;
			if (insert_error == Error::NONE)
				values.insert(values.begin() + index, nullptr);
			return insert_error;
		}
		void setPointerAtIndex(Clip* clip, int index) { values.at(index) = clip; }
		void delete_at_index_preserving_capacity(int index) {
			++removals;
			values.erase(values.begin() + index);
		}
	} arrangementOnlyClips;
	int deletions = 0;
	Clip* deleted_clip = nullptr;
	bool contains_clip_for_undo(Clip* clip) {
		for (auto* owned : arrangementOnlyClips.values)
			if (owned == clip)
				return true;
		return false;
	}
	void deleteClipObject(Clip* clip, bool, InstrumentRemoval) {
		++deletions;
		deleted_clip = clip;
	}
};
static Song* currentSong = nullptr;
static struct {
	RecordingMode recording = RecordingMode::ARRANGEMENT;
	int32_t record_pos = 64;
	int32_t getActualArrangementRecordPos() { return record_pos; }
} playbackHandler;
#include "audio_recording_rollover.inc"
} // namespace audio_recording_rollover_test
using namespace audio_recording_rollover_test;
TEST_GROUP(AudioRecordingRollover) {
	AudioClip current, original, copy;
	Output output;
	Song song;
	ModelStackWithTimelineCounter stack;
	void setup() override {
		panels::detail::active = panels::Id::Local;
		on_base = {};
		currentSong = &song;
		playbackHandler = {};
		current.output = original.output = copy.output = &output;
		current.beingRecordedFromClip = &original;
		original.clone_target = &copy;
		output.active = &current;
		output.clipInstances.values[0].clip = &current;
		song.arrangementOnlyClips.values = {&current};
		stack = {&song, &current};
	}
	void teardown() override {
		on_base = {};
		panels::detail::active = panels::Id::Local;
	}
	void rollover() {
		current.posReachedEnd(&stack);
	}
	void check_original_unchanged() {
		LONGS_EQUAL(128, output.clipInstances.values[0].length);
		LONGS_EQUAL(1, output.clipInstances.values.size());
		POINTERS_EQUAL(&original, current.beingRecordedFromClip);
		POINTERS_EQUAL(&current, output.active);
	}
};
TEST(AudioRecordingRollover, successful_transition_publishes_complete_copy) {
	rollover();
	LONGS_EQUAL(64, output.clipInstances.values[0].length);
	LONGS_EQUAL(2, output.clipInstances.values.size());
	POINTERS_EQUAL(&copy, output.clipInstances.values[1].clip);
	LONGS_EQUAL(64, output.clipInstances.values[1].pos);
	LONGS_EQUAL(64, output.clipInstances.values[1].length);
	POINTERS_EQUAL(&copy, output.active);
	POINTERS_EQUAL(nullptr, current.beingRecordedFromClip);
	POINTERS_EQUAL(&original, copy.beingRecordedFromClip);
	LONGS_EQUAL(1, copy.position_calls);
	LONGS_EQUAL(1, copy.paramManager.values.copies);
}
TEST(AudioRecordingRollover, clone_failure_preserves_original_extent_and_source_link) {
	original.clone_error = Error::INSUFFICIENT_RAM;
	rollover();
	check_original_unchanged();
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
	LONGS_EQUAL(0, song.deletions);
}
TEST(AudioRecordingRollover, song_publication_failure_discards_only_copy) {
	song.arrangementOnlyClips.insert_error = Error::INSUFFICIENT_RAM;
	rollover();
	check_original_unchanged();
	POINTERS_EQUAL(&copy, song.deleted_clip);
	POINTERS_EQUAL(&current, stack.clip);
	LONGS_EQUAL(0, output.clipInstances.inserts);
}
TEST(AudioRecordingRollover, instance_publication_failure_unpublishes_copy) {
	output.clipInstances.insert_error = Error::INSUFFICIENT_RAM;
	rollover();
	check_original_unchanged();
	LONGS_EQUAL(1, song.arrangementOnlyClips.removals);
	LONGS_EQUAL(1, song.arrangementOnlyClips.values.size());
	POINTERS_EQUAL(&copy, song.deleted_clip);
	POINTERS_EQUAL(&current, stack.clip);
}
TEST(AudioRecordingRollover, reservation_failures_do_not_clone_or_change_extent) {
	song.arrangementOnlyClips.reserve_ok = false;
	rollover();
	song.arrangementOnlyClips.reserve_ok = true;
	output.clipInstances.reserve_ok = false;
	rollover();
	LONGS_EQUAL(0, original.clone_calls);
	check_original_unchanged();
}
TEST(AudioRecordingRollover, relocation_during_clone_is_safe) {
	original.on_clone = [&] { output.clipInstances.relocate(); };
	rollover();
	POINTERS_EQUAL(&copy, output.active);
	LONGS_EQUAL(1, copy.paramManager.values.copies);
}
TEST(AudioRecordingRollover, callback_edit_to_instance_is_preserved) {
	original.on_clone = [&] { output.clipInstances.values[0].length = 99; };
	rollover();
	LONGS_EQUAL(99, output.clipInstances.values[0].length);
	LONGS_EQUAL(1, output.clipInstances.values.size());
	POINTERS_EQUAL(&copy, song.deleted_clip);
	POINTERS_EQUAL(&original, current.beingRecordedFromClip);
}
TEST(AudioRecordingRollover, missing_source_or_instance_does_not_clone) {
	current.beingRecordedFromClip = nullptr;
	rollover();
	current.beingRecordedFromClip = &original;
	output.clipInstances.search_result = -1;
	rollover();
	LONGS_EQUAL(0, original.clone_calls);
	check_original_unchanged();
}
TEST(AudioRecordingRollover, sequence_end_overflow_does_not_clone) {
	playbackHandler.record_pos = kMaxSequenceLength;
	rollover();
	LONGS_EQUAL(0, original.clone_calls);
	check_original_unchanged();
}
TEST(AudioRecordingRollover, owner_change_during_reservation_cancels) {
	song.arrangementOnlyClips.on_reserve = [] { panels::detail::active = panels::Id::Remote; };
	rollover();
	CHECK(panels::current() == panels::Id::Local);
	LONGS_EQUAL(0, original.clone_calls);
	check_original_unchanged();
}
TEST(AudioRecordingRollover, current_clip_destroyed_by_base_callback_cancels) {
	auto* target = new AudioClip;
	target->output = &output;
	stack.clip = target;
	on_base = [&] { delete target; };
	target->posReachedEnd(&stack);
	LONGS_EQUAL(0, original.clone_calls);
	LONGS_EQUAL(0, output.activations);
}
TEST(AudioRecordingRollover, output_destroyed_during_reservation_cancels) {
	auto* target = new Output;
	target->active = &current;
	current.output = original.output = target;
	song.arrangementOnlyClips.on_reserve = [&] { delete target; };
	rollover();
	LONGS_EQUAL(0, original.clone_calls);
}
TEST(AudioRecordingRollover, source_destroyed_during_reservation_cancels) {
	auto* target = new AudioClip;
	target->output = &output;
	current.beingRecordedFromClip = target;
	song.arrangementOnlyClips.on_reserve = [&] { delete target; };
	rollover();
	LONGS_EQUAL(0, output.clipInstances.inserts);
}
TEST(AudioRecordingRollover, activation_deleting_copy_prevents_positioning) {
	auto* target = new AudioClip;
	target->output = &output;
	original.clone_target = target;
	output.on_activate = [&] { delete target; };
	rollover();
	LONGS_EQUAL(1, output.activations);
}
TEST(AudioRecordingRollover, activation_changing_song_prevents_positioning) {
	output.on_activate = [] { currentSong = nullptr; };
	rollover();
	LONGS_EQUAL(0, copy.position_calls);
	LONGS_EQUAL(0, copy.paramManager.values.copies);
}
TEST(AudioRecordingRollover, positioning_changing_instance_prevents_parameter_copy) {
	copy.on_position = [&] { output.clipInstances.values[1].pos = 65; };
	rollover();
	LONGS_EQUAL(1, copy.position_calls);
	LONGS_EQUAL(0, copy.paramManager.values.copies);
}
TEST(AudioRecordingRollover, clone_adopted_by_callback_is_not_deleted) {
	original.on_clone = [&] { song.arrangementOnlyClips.values.push_back(&copy); };
	rollover();
	check_original_unchanged();
	LONGS_EQUAL(0, song.deletions);
}

TEST(AudioRecordingRollover, changed_source_length_during_clone_discards_unpublished_copy) {
	original.on_clone = [&] { current.loopLength = 96; };
	rollover();
	check_original_unchanged();
	LONGS_EQUAL(96, current.loopLength);
	POINTERS_EQUAL(&copy, song.deleted_clip);
	LONGS_EQUAL(0, song.arrangementOnlyClips.inserts);
}
TEST(AudioRecordingRollover, changed_recording_link_during_clone_is_preserved) {
	original.on_clone = [&] { current.beingRecordedFromClip = nullptr; };
	rollover();
	POINTERS_EQUAL(nullptr, current.beingRecordedFromClip);
	POINTERS_EQUAL(&copy, song.deleted_clip);
	LONGS_EQUAL(128, output.clipInstances.values[0].length);
}
TEST(AudioRecordingRollover, zero_closed_extent_does_not_clone) {
	playbackHandler.record_pos = 0;
	rollover();
	LONGS_EQUAL(0, original.clone_calls);
	check_original_unchanged();
}
TEST(AudioRecordingRollover, activation_editing_recording_link_prevents_positioning) {
	output.on_activate = [&] { copy.beingRecordedFromClip = nullptr; };
	rollover();
	LONGS_EQUAL(0, copy.position_calls);
	LONGS_EQUAL(0, copy.paramManager.values.copies);
}
TEST(AudioRecordingRollover, activation_editing_previous_instance_prevents_positioning) {
	output.on_activate = [&] { output.clipInstances.values[0].length = 17; };
	rollover();
	LONGS_EQUAL(17, output.clipInstances.values[0].length);
	LONGS_EQUAL(0, copy.position_calls);
}
