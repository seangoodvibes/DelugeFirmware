#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <array>
#include <functional>

namespace midi_feedback_sweep_test {
namespace panels = deluge::gui::ui_session;
struct TimelineCounter {};
struct Clip : TimelineCounter {};
static Clip clip;
static Clip* current_clip = &clip;
static Clip* getCurrentClip() {
	return current_clip;
}
static int song;
static int* currentSong = &song;
struct ModelStackWithTimelineCounter {
	TimelineCounter* timeline = &clip;
	bool timelineCounterIsSet() { return timeline != nullptr; }
	TimelineCounter* getTimelineCounter() { return timeline; }
	TimelineCounter* getTimelineCounterAllowNull() { return timeline; }
};
struct ModelStack {
	ModelStackWithTimelineCounter timeline;
	ModelStackWithTimelineCounter* addTimelineCounter(Clip* target) {
		timeline.timeline = target;
		return &timeline;
	}
};
constexpr size_t MODEL_STACK_MAX_SIZE = sizeof(ModelStack);
static ModelStack stack;
static ModelStack* setupModelStackWithSong(char*, int*) {
	return &stack;
}
struct view_fixture {
	int modLength = 0;
	int modPos = 0;
	ModelStackWithTimelineCounter activeModControllableModelStack;
};
static panels::State<view_fixture> views;
static view_fixture& view_for_session() {
	return views.active();
}
struct ModelStackWithAutoParam;
struct param_fixture {
	bool automated = false;
	int position_reads = 0;
	int last_position = -1;
	bool isAutomated() { return automated; }
	int getCurrentValue() { return 9; }
	int getValuePossiblyAtPos(int position, ModelStackWithAutoParam*) {
		++position_reads;
		last_position = position;
		return 12;
	}
};
struct collection_fixture {
	int paramValueToKnobPos(int value, ModelStackWithAutoParam*) { return value; }
};
struct ModelStackWithAutoParam {
	param_fixture* autoParam = nullptr;
	collection_fixture* paramCollection = nullptr;
};
struct MidiFollow {
	using FeedbackChannelTypes = std::array<int, 2>;
	size_t target_count = 1;
	Clip* selected_clip = &clip;
	param_fixture parameter;
	collection_fixture collection;
	ModelStackWithAutoParam parameter_stack{&parameter, &collection};
	int lookups = 0;
	int sends = 0;
	int last_value = -1;
	std::function<void()> on_send;
	std::function<void()> on_lookup;
	uint8_t ccToSoundParam[128]{};
	uint8_t ccToGlobalParam[128]{};
	size_t getChannelTypesForFeedback(FeedbackChannelTypes&) { return target_count; }
	Clip* getSelectedOrActiveClip() { return selected_clip; }
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter*, Clip*, int, int, bool) {
		++lookups;
		if (on_lookup)
			on_lookup();
		return &parameter_stack;
	}
	void sendCCForMidiFollowFeedback(FeedbackChannelTypes&, size_t, int, int value) {
		++sends;
		last_value = value;
		if (on_send)
			on_send();
	}
	void sendCCWithoutModelStackForMidiFollowFeedback(bool);
};
#include "midi_feedback_sweep.inc"
} // namespace midi_feedback_sweep_test
using namespace midi_feedback_sweep_test;
TEST_GROUP(MidiFeedbackSweep) {
	MidiFollow follow;
	void setup() override {
		panels::detail::active = panels::Id::Local;
		views = {};
		currentSong = &song;
		current_clip = &clip;
	}
	void teardown() override {
		panels::detail::active = panels::Id::Local;
	}
};
TEST(MidiFeedbackSweep, normal_feedback_and_automation_filtering) {
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(128, follow.sends);
	LONGS_EQUAL(9, follow.last_value);
	follow.sends = 0;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(true);
	LONGS_EQUAL(0, follow.sends);
	follow.parameter.automated = true;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(true);
	LONGS_EQUAL(128, follow.sends);
}
TEST(MidiFeedbackSweep, step_editing_uses_owner_position_and_suppresses_automation) {
	for (auto owner : {panels::Id::Local, panels::Id::Remote}) {
		panels::Scope scope(owner);
		view_for_session().modLength = 4;
		view_for_session().modPos = owner == panels::Id::Local ? 19 : 31;
		follow.sends = 0;
		follow.sendCCWithoutModelStackForMidiFollowFeedback(true);
		LONGS_EQUAL(0, follow.sends);
		follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
		LONGS_EQUAL(128, follow.sends);
		LONGS_EQUAL(view_for_session().modPos, follow.parameter.last_position);
		LONGS_EQUAL(12, follow.last_value);
	}
}
TEST(MidiFeedbackSweep, missing_collection_or_song_does_not_send) {
	follow.parameter_stack.paramCollection = nullptr;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(0, follow.sends);
	follow.parameter_stack.paramCollection = &follow.collection;
	currentSong = nullptr;
	follow.lookups = 0;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(0, follow.lookups);
	LONGS_EQUAL(0, follow.sends);
}
TEST(MidiFeedbackSweep, sending_stops_sweep_after_context_changes) {
	for (int change = 0; change < 3; ++change) {
		currentSong = &song;
		current_clip = &clip;
		follow.sends = follow.lookups = 0;
		follow.on_send = [&] {
			if (change == 0)
				currentSong = nullptr;
			if (change == 1)
				current_clip = nullptr;
			if (change == 2)
				panels::detail::active = panels::Id::Remote;
		};
		follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
		LONGS_EQUAL(1, follow.sends);
		LONGS_EQUAL(1, follow.lookups);
		CHECK(panels::current() == panels::Id::Local);
	}
}
TEST(MidiFeedbackSweep, changed_lookup_context_prevents_parameter_access) {
	follow.on_lookup = [&] { currentSong = nullptr; };
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(1, follow.lookups);
	LONGS_EQUAL(0, follow.sends);
}

TEST(MidiFeedbackSweep, absent_targets_and_unlearned_parameters_do_not_send) {
	follow.target_count = 0;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(0, follow.lookups);
	follow.target_count = 1;
	follow.selected_clip = nullptr;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(0, follow.lookups);
	follow.selected_clip = &clip;
	follow.parameter_stack.autoParam = nullptr;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(128, follow.lookups);
	LONGS_EQUAL(0, follow.sends);
}
