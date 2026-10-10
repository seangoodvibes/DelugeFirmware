#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include "modulation/params/param.h"
#include "util/lifetime.h"
#include <array>
#include <functional>
#include <memory>

namespace midi_feedback_sweep_test {
namespace panels = deluge::gui::ui_session;
namespace params = deluge::modulation::params;
constexpr int PARAM_ID_NONE = 255;
struct MIDICable {};
struct ModelStackWithTimelineCounter;
static std::function<void(ModelStackWithTimelineCounter*)> on_clone;
static int clone_calls = 0;
static bool clone_result = false;
static Error clone_error = Error::NONE;
struct TimelineCounter {
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* model_stack, Error* error = nullptr) {
		if (error)
			*error = clone_error;
		++clone_calls;
		if (on_clone)
			on_clone(model_stack);
		return clone_result;
	}
};
struct RootUI {};
static RootUI root_ui;
static RootUI* current_root = &root_ui;
static RootUI* getRootUI() {
	return current_root;
}

struct Output {
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
};
struct Clip : TimelineCounter {
	Output* output = nullptr;
	mutable deluge::lifetime::lifetime_source lifetime_source;
	auto watch_lifetime() const { return deluge::lifetime::lifetime_watch{lifetime_source}; }
};
static std::function<void()> on_refresh;
struct automation_fixture : RootUI {
	bool possiblyRefreshAutomationEditorGrid(Clip*, params::Kind, int) {
		if (on_refresh)
			on_refresh();
		return false;
	}
};
struct performance_fixture : RootUI {
	bool possiblyRefreshPerformanceViewDisplay(params::Kind, int, int) { return false; }
};
static automation_fixture automation;
static performance_fixture performance;
static auto& automation_view_for_session() {
	return automation;
}
static auto& performance_view_for_session() {
	return performance;
}
static struct {
	bool midiFollowDisplayParam = false;
} midiEngine;
namespace MidiTakeover {
static int calculateKnobPos(MIDICable&, int, int value, void*, bool, int, bool) {
	return value;
}
} // namespace MidiTakeover
static Clip clip;
static Clip* current_clip = &clip;
static Clip* getCurrentClip() {
	return current_clip;
}
struct song_fixture {
	const Clip* registered_clip = nullptr;
	bool contains_clip_for_undo(const Clip* target) { return target && target == registered_clip; }
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
static song_fixture song;
static song_fixture* currentSong = &song;
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
static ModelStack* setupModelStackWithSong(char*, song_fixture*) {
	return &stack;
}
struct view_fixture {
	int popup_calls = 0;
	int popup_id = -1;
	void displayModEncoderValuePopup(params::Kind, int id, int) {
		++popup_calls;
		popup_id = id;
	}
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
	int writes = 0;
	std::function<void()> on_write;
	void setValuePossiblyForRegion(int, ModelStackWithAutoParam*, int, int) {
		++writes;
		if (on_write)
			on_write();
	}
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
	params::Kind getParamKind() { return params::Kind::PATCHED; }
	int knobPosToParamValue(int value, ModelStackWithAutoParam*) { return value; }
	int paramValueToKnobPos(int value, ModelStackWithAutoParam*) { return value; }
};
struct ModelStackWithAutoParam {
	int paramId = 0;
	param_fixture* autoParam = nullptr;
	collection_fixture* paramCollection = nullptr;
};
struct MidiFollow {
	using FeedbackChannelTypes = std::array<int, 2>;
	size_t target_count = 1;
	Clip* selected_clip = &clip;
	param_fixture parameter;
	collection_fixture collection;
	ModelStackWithAutoParam parameter_stack{0, &parameter, &collection};
	Clip* lookup_clip = nullptr;
	int lookups = 0;
	int sends = 0;
	int last_value = -1;
	std::function<void()> on_send;
	std::function<void()> on_lookup;
	uint8_t ccToSoundParam[128]{};
	uint8_t ccToGlobalParam[128]{};
	size_t getChannelTypesForFeedback(FeedbackChannelTypes&) { return target_count; }
	Clip* getSelectedOrActiveClip() { return selected_clip; }
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter*, Clip* target_clip, int, int, bool) {
		lookup_clip = target_clip;
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
	void handleReceivedCC(MIDICable&, ModelStackWithTimelineCounter&, Clip*, int32_t, int32_t);
};
#include "midi_feedback_sweep.inc"
} // namespace midi_feedback_sweep_test
using namespace midi_feedback_sweep_test;
TEST_GROUP(MidiFeedbackSweep) {
	MidiFollow follow;
	MIDICable cable;
	void receive(int cc = 7, int value = 64) {
		stack.timeline.timeline = &clip;
		follow.handleReceivedCC(cable, stack.timeline, &clip, cc, value);
	}
	void setup() override {
		panels::detail::active = panels::Id::Local;
		views = {};
		current_root = &root_ui;
		on_refresh = {};
		midiEngine = {};
		on_clone = {};
		clone_calls = 0;
		clone_result = false;
		clone_error = Error::NONE;
		currentSong = &song;
		song.registered_clip = nullptr;
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
		song.registered_clip = nullptr;
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

TEST(MidiFeedbackSweep, incoming_cc_uses_arrangement_clone_for_parameter_lookup) {
	Clip cloned_clip;
	clone_result = true;
	on_clone = [&](ModelStackWithTimelineCounter* model_stack) {
		model_stack->timeline = &cloned_clip;
		song.registered_clip = &cloned_clip;
	};
	receive();
	POINTERS_EQUAL(&cloned_clip, follow.lookup_clip);
	LONGS_EQUAL(1, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, unlearned_or_invalid_cc_does_not_clone) {
	follow.ccToSoundParam[7] = follow.ccToGlobalParam[7] = PARAM_ID_NONE;
	receive();
	receive(-1);
	receive(128);
	receive(0, -1);
	receive(0, 128);
	LONGS_EQUAL(0, clone_calls);
	LONGS_EQUAL(0, follow.lookups);
}
TEST(MidiFeedbackSweep, incoming_cc_stops_when_clone_changes_context) {
	for (int change = 0; change < 3; ++change) {
		currentSong = &song;
		song.registered_clip = nullptr;
		current_clip = &clip;
		on_clone = [&](ModelStackWithTimelineCounter*) {
			if (change == 0)
				currentSong = nullptr;
			if (change == 1)
				current_clip = nullptr;
			if (change == 2)
				panels::detail::active = panels::Id::Remote;
		};
		receive();
		LONGS_EQUAL(0, follow.lookups);
		LONGS_EQUAL(0, follow.parameter.writes);
		CHECK(panels::current() == panels::Id::Local);
	}
}
TEST(MidiFeedbackSweep, incoming_cc_without_clone_keeps_original_target) {
	receive();
	POINTERS_EQUAL(&clip, follow.lookup_clip);
	LONGS_EQUAL(1, follow.parameter.writes);
}

TEST(MidiFeedbackSweep, incoming_cc_rejects_mismatched_timeline_and_missing_song) {
	Clip other_clip;
	stack.timeline.timeline = &other_clip;
	follow.handleReceivedCC(cable, stack.timeline, &clip, 7, 64);
	currentSong = nullptr;
	receive();
	LONGS_EQUAL(0, clone_calls);
	LONGS_EQUAL(0, follow.lookups);
}

TEST(MidiFeedbackSweep, incoming_cc_incomplete_stack_or_changed_lookup_does_not_write) {
	follow.parameter_stack.paramCollection = nullptr;
	receive();
	LONGS_EQUAL(0, follow.parameter.writes);
	follow.parameter_stack.paramCollection = &follow.collection;
	follow.on_lookup = [&] { currentSong = nullptr; };
	receive();
	LONGS_EQUAL(0, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, incoming_cc_caches_display_identity_before_parameter_write) {
	midiEngine.midiFollowDisplayParam = true;
	follow.parameter_stack.paramId = 42;
	follow.parameter.on_write = [&] {
		follow.parameter_stack.paramCollection = nullptr;
		follow.parameter_stack.autoParam = nullptr;
		follow.parameter_stack.paramId = 99;
	};
	receive();
	LONGS_EQUAL(1, follow.parameter.writes);
	LONGS_EQUAL(1, view_for_session().popup_calls);
	LONGS_EQUAL(42, view_for_session().popup_id);
}
TEST(MidiFeedbackSweep, incoming_cc_changed_write_context_suppresses_display) {
	midiEngine.midiFollowDisplayParam = true;
	for (int change = 0; change < 3; ++change) {
		currentSong = &song;
		song.registered_clip = nullptr;
		current_clip = &clip;
		follow.parameter.on_write = [&] {
			if (change == 0)
				currentSong = nullptr;
			if (change == 1)
				current_clip = nullptr;
			if (change == 2)
				panels::detail::active = panels::Id::Remote;
		};
		receive();
		LONGS_EQUAL(0, view_for_session().popup_calls);
		CHECK(panels::current() == panels::Id::Local);
	}
}
TEST(MidiFeedbackSweep, incoming_cc_changed_refresh_context_suppresses_popup) {
	midiEngine.midiFollowDisplayParam = true;
	current_root = &automation;
	on_refresh = [&] { currentSong = nullptr; };
	receive();
	LONGS_EQUAL(1, follow.parameter.writes);
	LONGS_EQUAL(0, view_for_session().popup_calls);
}

TEST(MidiFeedbackSweep, failed_arrangement_clone_does_not_edit_original) {
	clone_error = Error::INSUFFICIENT_RAM;
	receive();
	LONGS_EQUAL(1, clone_calls);
	LONGS_EQUAL(0, follow.lookups);
	LONGS_EQUAL(0, follow.parameter.writes);
	POINTERS_EQUAL(&clip, stack.timeline.timeline);
}

TEST(MidiFeedbackSweep, destroyed_feedback_target_stops_before_sending) {
	auto* target = new Clip;
	follow.selected_clip = target;
	follow.on_lookup = [&] { delete target; };
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(1, follow.lookups);
	LONGS_EQUAL(0, follow.sends);
}
TEST(MidiFeedbackSweep, destroyed_incoming_target_stops_before_parameter_write) {
	auto* target = new Clip;
	stack.timeline.timeline = target;
	follow.on_lookup = [&] { delete target; };
	follow.handleReceivedCC(cable, stack.timeline, target, 7, 64);
	LONGS_EQUAL(0, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, destroyed_recording_clone_stops_post_write_display) {
	auto* target = new Clip;
	clone_result = true;
	on_clone = [&](ModelStackWithTimelineCounter* model_stack) {
		model_stack->timeline = target;
		song.registered_clip = target;
	};
	midiEngine.midiFollowDisplayParam = true;
	follow.parameter.on_write = [&] { delete target; };
	receive();
	LONGS_EQUAL(1, follow.parameter.writes);
	LONGS_EQUAL(0, view_for_session().popup_calls);
}
TEST(MidiFeedbackSweep, reused_feedback_target_address_does_not_continue_sweep) {
	auto* target = new Clip;
	follow.selected_clip = target;
	follow.on_send = [&] {
		std::destroy_at(target);
		target = std::construct_at(target);
	};
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(1, follow.sends);
	LONGS_EQUAL(1, follow.lookups);
	delete target;
}
TEST(MidiFeedbackSweep, retiring_target_never_reaches_parameter_services) {
	auto* target = new Clip;
	target->lifetime_source.retire();
	follow.selected_clip = target;
	stack.timeline.timeline = target;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	follow.handleReceivedCC(cable, stack.timeline, target, 7, 64);
	LONGS_EQUAL(0, follow.lookups);
	LONGS_EQUAL(0, clone_calls);
	delete target;
}

TEST(MidiFeedbackSweep, feedback_output_deleted_during_lookup_prevents_send) {
	Clip target;
	target.output = new Output;
	follow.selected_clip = &target;
	follow.on_lookup = [&] { delete target.output; };
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(1, follow.lookups);
	LONGS_EQUAL(0, follow.sends);
}
TEST(MidiFeedbackSweep, feedback_output_reuse_after_send_stops_sweep) {
	Clip target;
	target.output = new Output;
	follow.selected_clip = &target;
	follow.on_send = [&] {
		std::destroy_at(target.output);
		target.output = std::construct_at(target.output);
	};
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(1, follow.lookups);
	LONGS_EQUAL(1, follow.sends);
	delete target.output;
}
TEST(MidiFeedbackSweep, incoming_output_deleted_during_lookup_prevents_write) {
	Clip target;
	target.output = new Output;
	stack.timeline.timeline = &target;
	follow.on_lookup = [&] { delete target.output; };
	follow.handleReceivedCC(cable, stack.timeline, &target, 7, 64);
	LONGS_EQUAL(0, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, recording_clone_output_destroyed_during_write_suppresses_display) {
	Clip target;
	target.output = new Output;
	clone_result = true;
	on_clone = [&](ModelStackWithTimelineCounter* model_stack) {
		model_stack->timeline = &target;
		song.registered_clip = &target;
	};
	midiEngine.midiFollowDisplayParam = true;
	follow.parameter.on_write = [&] { delete target.output; };
	receive();
	LONGS_EQUAL(1, follow.parameter.writes);
	LONGS_EQUAL(0, view_for_session().popup_calls);
}
TEST(MidiFeedbackSweep, changed_output_during_lookup_prevents_send_and_write) {
	Clip target;
	Output original_output, replacement_output;
	target.output = &original_output;
	follow.selected_clip = &target;
	follow.on_lookup = [&] { target.output = &replacement_output; };
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(0, follow.sends);
	target.output = &original_output;
	stack.timeline.timeline = &target;
	follow.handleReceivedCC(cable, stack.timeline, &target, 7, 64);
	LONGS_EQUAL(0, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, retired_output_never_reaches_parameter_services) {
	Clip target;
	Output output;
	target.output = &output;
	output.lifetime_source.retire();
	follow.selected_clip = &target;
	stack.timeline.timeline = &target;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	follow.handleReceivedCC(cable, stack.timeline, &target, 7, 64);
	LONGS_EQUAL(0, follow.lookups);
	LONGS_EQUAL(0, clone_calls);
}

TEST(MidiFeedbackSweep, feedback_song_reuse_after_send_cancels_remaining_parameters) {
	follow.on_send = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(1, follow.sends);
	LONGS_EQUAL(1, follow.lookups);
}
TEST(MidiFeedbackSweep, feedback_song_retirement_during_lookup_prevents_send) {
	song_fixture retiring_song;
	currentSong = &retiring_song;
	follow.on_lookup = [&] { retiring_song.lifetime.retire(); };
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	LONGS_EQUAL(1, follow.lookups);
	LONGS_EQUAL(0, follow.sends);
	currentSong = &song;
}
TEST(MidiFeedbackSweep, incoming_cc_song_reuse_during_clone_prevents_lookup) {
	on_clone = [](ModelStackWithTimelineCounter*) {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	receive();
	LONGS_EQUAL(1, clone_calls);
	LONGS_EQUAL(0, follow.lookups);
	LONGS_EQUAL(0, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, incoming_cc_song_retirement_during_lookup_prevents_write) {
	song_fixture retiring_song;
	currentSong = &retiring_song;
	follow.on_lookup = [&] { retiring_song.lifetime.retire(); };
	receive();
	LONGS_EQUAL(1, follow.lookups);
	LONGS_EQUAL(0, follow.parameter.writes);
	currentSong = &song;
}
TEST(MidiFeedbackSweep, incoming_cc_song_reuse_during_write_suppresses_popup) {
	midiEngine.midiFollowDisplayParam = true;
	follow.parameter.on_write = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	receive();
	LONGS_EQUAL(1, follow.parameter.writes);
	LONGS_EQUAL(0, view_for_session().popup_calls);
}
TEST(MidiFeedbackSweep, retired_song_rejects_feedback_and_incoming_cc) {
	song_fixture retiring_song;
	retiring_song.lifetime.retire();
	currentSong = &retiring_song;
	follow.sendCCWithoutModelStackForMidiFollowFeedback(false);
	receive();
	LONGS_EQUAL(0, clone_calls);
	LONGS_EQUAL(0, follow.lookups);
	LONGS_EQUAL(0, follow.sends);
	LONGS_EQUAL(0, follow.parameter.writes);
	currentSong = &song;
}

TEST(MidiFeedbackSweep, freed_unregistered_clone_is_rejected_before_watch_acquisition) {
	auto* target = new Clip;
	delete target;
	clone_result = true;
	on_clone = [&](ModelStackWithTimelineCounter* model_stack) { model_stack->timeline = target; };
	receive();
	LONGS_EQUAL(0, follow.lookups);
	LONGS_EQUAL(0, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, source_unregistration_during_clone_cancels_lookup) {
	song.registered_clip = &clip;
	on_clone = [&](ModelStackWithTimelineCounter*) { song.registered_clip = nullptr; };
	receive();
	LONGS_EQUAL(0, follow.lookups);
	LONGS_EQUAL(0, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, replacement_unregistration_during_lookup_cancels_write) {
	Clip target;
	clone_result = true;
	on_clone = [&](ModelStackWithTimelineCounter* model_stack) {
		model_stack->timeline = &target;
		song.registered_clip = &target;
	};
	follow.on_lookup = [&] { song.registered_clip = nullptr; };
	receive();
	LONGS_EQUAL(1, follow.lookups);
	LONGS_EQUAL(0, follow.parameter.writes);
}
TEST(MidiFeedbackSweep, replacement_unregistration_during_write_suppresses_popup) {
	Clip target;
	clone_result = true;
	on_clone = [&](ModelStackWithTimelineCounter* model_stack) {
		model_stack->timeline = &target;
		song.registered_clip = &target;
	};
	midiEngine.midiFollowDisplayParam = true;
	follow.parameter.on_write = [&] { song.registered_clip = nullptr; };
	receive();
	LONGS_EQUAL(1, follow.parameter.writes);
	LONGS_EQUAL(0, view_for_session().popup_calls);
}
