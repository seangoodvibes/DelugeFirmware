#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstring>
#include <functional>
#include <memory>
#include <new>
namespace audio_clone_lifetime_test {
namespace panels = deluge::gui::ui_session;
static std::function<void()> on_allocate, on_parameters, on_sample;
static bool allocation_fails = false;
static Error parameter_error = Error::NONE;
static int allocations = 0, frees = 0, parameter_calls = 0, sample_calls = 0;
struct TimelineCounter {};
struct song_fixture : TimelineCounter {};
static song_fixture song;
static song_fixture* currentSong = &song;
struct GeneralMemoryAllocator {
	static GeneralMemoryAllocator& get() {
		static GeneralMemoryAllocator allocator;
		return allocator;
	}
	void* allocMaxSpeed(size_t size) {
		++allocations;
		auto* result = allocation_fails ? nullptr : ::operator new(size, std::nothrow);
		if (on_allocate)
			on_allocate();
		return result;
	}
};
static void delugeDealloc(void* pointer) {
	++frees;
	::operator delete(pointer);
}
struct Output {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
};
struct AudioClip;
struct ModelStackWithTimelineCounter {
	song_fixture* song = &audio_clone_lifetime_test::song;
	TimelineCounter* clip = nullptr;
	TimelineCounter* getTimelineCounterAllowNull() { return clip; }
	void setTimelineCounter(TimelineCounter* value) { clip = value; }
};
struct AudioClip : TimelineCounter {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
	Output* output = nullptr;
	bool activeIfNoSolo = true, soloingInSessionMode = true;
	int attack = 5, voicePriority = 7, loop_length = 64;
	struct {
		Error cloneParamCollectionsFrom(const void*, bool) {
			++parameter_calls;
			const auto result = parameter_error;
			if (on_parameters)
				on_parameters();
			return result;
		}
	} paramManager;
	struct {
		bool reversed = false;
		bool isCurrentlyReversed() const { return reversed; }
	} sampleControls;
	struct {
		void beenClonedFrom(const void*, bool) {
			++sample_calls;
			if (on_sample)
				on_sample();
		}
	} sampleHolder;
	void copyBasicsFrom(const AudioClip* source) { loop_length = source->loop_length; }
	Error clone(ModelStackWithTimelineCounter*, bool) const;
};
using Clip = AudioClip;
#include "audio_clone_lifetime.inc"
} // namespace audio_clone_lifetime_test
using namespace audio_clone_lifetime_test;
TEST_GROUP(AudioCloneLifetime) {
	AudioClip source;
	Output output;
	ModelStackWithTimelineCounter stack;
	void setup() override {
		on_allocate = on_parameters = on_sample = {};
		allocation_fails = false;
		parameter_error = Error::NONE;
		allocations = frees = parameter_calls = sample_calls = 0;
		panels::detail::active = panels::Id::Local;
		currentSong = &song;
		source.output = &output;
		stack.clip = &source;
	}
	void teardown() override {
		on_allocate = on_parameters = on_sample = {};
		panels::detail::active = panels::Id::Local;
	}
};
TEST(AudioCloneLifetime, normal_clone_publishes_after_sample_setup) {
	on_sample = [&] { POINTERS_EQUAL(&source, stack.clip); };
	CHECK(source.clone(&stack, false) == Error::NONE);
	CHECK(stack.clip != &source);
	POINTERS_EQUAL(&output, static_cast<AudioClip*>(stack.clip)->output);
	CHECK_FALSE(static_cast<AudioClip*>(stack.clip)->activeIfNoSolo);
	LONGS_EQUAL(source.attack, static_cast<AudioClip*>(stack.clip)->attack);
	static_cast<AudioClip*>(stack.clip)->~AudioClip();
	delugeDealloc(stack.clip);
	LONGS_EQUAL(1, frees);
}
TEST(AudioCloneLifetime, distinct_incoming_timeline_is_supported) {
	AudioClip recording_clip;
	stack.clip = &recording_clip;
	on_sample = [&] { POINTERS_EQUAL(&recording_clip, stack.clip); };
	CHECK(source.clone(&stack, false) == Error::NONE);
	CHECK(stack.clip != &recording_clip);
	static_cast<AudioClip*>(stack.clip)->~AudioClip();
	delugeDealloc(stack.clip);
}
TEST(AudioCloneLifetime, allocation_failure_preserves_original) {
	allocation_fails = true;
	CHECK(source.clone(&stack, false) == Error::INSUFFICIENT_RAM);
	POINTERS_EQUAL(&source, stack.clip);
	LONGS_EQUAL(0, parameter_calls);
	LONGS_EQUAL(0, frees);
}
TEST(AudioCloneLifetime, parameter_failure_discards_only_copy) {
	parameter_error = Error::INSUFFICIENT_RAM;
	CHECK(source.clone(&stack, false) == Error::INSUFFICIENT_RAM);
	POINTERS_EQUAL(&source, stack.clip);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(0, sample_calls);
}
TEST(AudioCloneLifetime, source_destroyed_during_allocation_cancels_before_copy) {
	auto* target = new AudioClip;
	target->output = &output;
	stack.clip = target;
	on_allocate = [&] { delete target; };
	CHECK(target->clone(&stack, false) == Error::BUG);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(0, parameter_calls);
}
TEST(AudioCloneLifetime, source_destroyed_during_parameters_cancels_before_sample) {
	auto* target = new AudioClip;
	target->output = &output;
	stack.clip = target;
	on_parameters = [&] { delete target; };
	CHECK(target->clone(&stack, false) == Error::BUG);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(0, sample_calls);
}
TEST(AudioCloneLifetime, reused_source_address_does_not_publish_copy) {
	auto* target = new AudioClip;
	stack.clip = target;
	on_parameters = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
	};
	CHECK(target->clone(&stack, false) == Error::BUG);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(0, sample_calls);
	delete target;
}
TEST(AudioCloneLifetime, output_destroyed_during_parameters_cancels) {
	source.output = new Output;
	on_parameters = [&] { delete source.output; };
	CHECK(source.clone(&stack, false) == Error::BUG);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(0, sample_calls);
	POINTERS_EQUAL(&source, stack.clip);
}
TEST(AudioCloneLifetime, sample_callback_retargeting_stack_is_preserved) {
	AudioClip replacement;
	on_sample = [&] { stack.clip = &replacement; };
	CHECK(source.clone(&stack, false) == Error::BUG);
	LONGS_EQUAL(1, frees);
	POINTERS_EQUAL(&replacement, stack.clip);
}
TEST(AudioCloneLifetime, owner_change_cancels_and_restores_initiating_panel) {
	on_parameters = [] { panels::detail::active = panels::Id::Remote; };
	CHECK(source.clone(&stack, false) == Error::BUG);
	CHECK(panels::current() == panels::Id::Local);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(0, sample_calls);
}
TEST(AudioCloneLifetime, retired_source_or_output_does_not_allocate) {
	output.lifetime_source.retire();
	CHECK(source.clone(&stack, false) == Error::BUG);
	source.output = nullptr;
	source.lifetime_source.retire();
	CHECK(source.clone(&stack, false) == Error::BUG);
	CHECK(source.clone(nullptr, false) == Error::BUG);
	LONGS_EQUAL(0, allocations);
}
TEST(AudioCloneLifetime, song_change_during_sample_setup_does_not_publish) {
	on_sample = [] { currentSong = nullptr; };
	CHECK(source.clone(&stack, false) == Error::BUG);
	LONGS_EQUAL(1, frees);
	POINTERS_EQUAL(&source, stack.clip);
}

TEST(AudioCloneLifetime, different_incoming_clip_destroyed_during_parameters_cancels) {
	auto* recording_clip = new AudioClip;
	stack.clip = recording_clip;
	on_parameters = [&] { delete recording_clip; };
	CHECK(source.clone(&stack, false) == Error::BUG);
	LONGS_EQUAL(1, frees);
	LONGS_EQUAL(0, sample_calls);
}
TEST(AudioCloneLifetime, retiring_incoming_clip_prevents_allocation) {
	AudioClip recording_clip;
	recording_clip.lifetime_source.retire();
	stack.clip = &recording_clip;
	CHECK(source.clone(&stack, false) == Error::BUG);
	LONGS_EQUAL(0, allocations);
}

TEST(AudioCloneLifetime, song_timeline_is_not_treated_as_a_clip) {
	stack.clip = &song;
	on_sample = [&] { POINTERS_EQUAL(&song, stack.clip); };
	CHECK(source.clone(&stack, false) == Error::NONE);
	auto* copy = static_cast<AudioClip*>(stack.clip);
	POINTERS_EQUAL(&output, copy->output);
	copy->~AudioClip();
	delugeDealloc(copy);
}
