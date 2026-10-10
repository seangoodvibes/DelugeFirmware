#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
namespace knob_indicator_test {
namespace session = ::deluge::gui::ui_session;
constexpr int32_t operator""_i32(unsigned long long value) {
	return static_cast<int32_t>(value);
}
constexpr int kKnobPosOffset = 64, kMaxKnobPos = 128, UI_MODE_STUTTERING = 1;
namespace params {
enum class Kind { NORMAL, PATCH_CABLE };
}
static std::function<void()> on_lookup, on_value, on_grab, on_mod_leds, on_redraw, on_resolve, on_activate;
constexpr int NUM_LEVEL_INDICATORS = 2;
static int lookup_calls = 0;
struct root_fixture {
	bool automation_editor = false;
	int renders = 0;
	bool inAutomationEditor() { return automation_editor; }
	void displayAutomation() { ++renders; }
};
static root_fixture root, automation;
static session::State<root_fixture*> roots;
static root_fixture* getRootUI() {
	return roots.active();
}
static root_fixture& automation_view_for_session() {
	return automation;
}
static int song, replacement_song;
static int* currentSong = &song;
struct ModelStackWithAutoParam;
struct ParamCollection {
	bool has_value = false;
	int32_t value = 0;
	params::Kind kind = params::Kind::NORMAL;
	bool has_current_value(int32_t) { return has_value; }
	int32_t get_current_value(int32_t) { return value; }
	params::Kind getParamKind() { return kind; }
	int32_t paramValueToKnobPos(int32_t value, ModelStackWithAutoParam*) { return value; }
};
struct AutoParam {
	int32_t value = 0;
	int32_t getValuePossiblyAtPos(uint32_t, ModelStackWithAutoParam*) {
		if (on_value)
			on_value();
		return value;
	}
};
struct ParamManager {
	bool compatible = true;
	int grabs = 0;
	bool matches_type(int) { return compatible; }
	ParamManager* toForTimeline() { return this; }
	template <class T>
	void grabValuesFromPos(uint32_t, T*) {
		++grabs;
		if (on_grab)
			on_grab();
	}
};
struct ModControllable {
	int required_param_manager_type() { return 0; }
	ModelStackWithAutoParam* result = nullptr;
	int32_t fallback = -64;
	template <class T>
	ModelStackWithAutoParam* getParamFromModEncoder(uint8_t, T*, bool) {
		++lookup_calls;
		if (on_lookup)
			on_lookup();
		return result;
	}
	int32_t getKnobPosForNonExistentParam(uint8_t, ModelStackWithAutoParam*) { return fallback; }
};
using ModControllableAudio = ModControllable;
struct ModelStackWithAutoParam {
	ModelStackWithAutoParam* addTimelineCounter(void* value) {
		timeline = value;
		return this;
	}
	void addOtherTwoThingsButNoNoteRow(ModControllable* target, ParamManager* manager) {
		modControllable = target;
		paramManager = manager;
	}
	ParamManager* paramManager = nullptr;
	void* timeline = nullptr;
	int* song = nullptr;
	void* getTimelineCounterAllowNull() const { return timeline; }
	bool timelineCounterIsSet() const { return timeline != nullptr; }
	ModControllable* modControllable = nullptr;
	AutoParam* autoParam = nullptr;
	ParamCollection* paramCollection = nullptr;
	int32_t paramId = 0;
};
using ModelStackWithTimelineCounter = ModelStackWithAutoParam;
struct TimelineCounter {
	TimelineCounter* redirected = this;
	ModControllable* target = nullptr;
	ParamManager* manager = nullptr;
	int activations = 0;
	TimelineCounter* getTimelineCounterToRecordTo() {
		if (on_resolve)
			on_resolve();
		return redirected;
	}
	void getActiveModControllable(ModelStackWithTimelineCounter* stack) {
		++activations;
		stack->addOtherTwoThingsButNoNoteRow(target, manager);
		if (on_activate)
			on_activate();
	}
};
static ModelStackWithAutoParam* setupModelStackWithSong(ModelStackWithAutoParam* stack, int* song) {
	stack->song = song;
	return stack;
}
static bool rootUIIsClipMinderScreen() {
	return false;
}
static int redraw_calls = 0;
static void uiNeedsRendering(root_fixture*, int) {
	++redraw_calls;
	if (on_redraw)
		on_redraw();
}
static bool bipolar = false, quantized = false, stuttering = false;
static bool isParamBipolar(params::Kind, int32_t) {
	return bipolar;
}
static bool isParamQuantizedStutter(params::Kind, int32_t, ModControllableAudio*) {
	return quantized;
}
static bool isUIModeActive(int) {
	return stuttering;
}
namespace indicator_leds {
struct result {
	int calls = 0;
	int32_t level = -999;
	bool bipolar = false;
};
static session::State<std::array<result, 2>> outputs;
static session::State<std::array<bool, 2>> blinking;
static session::State<int> clears;
static bool isKnobIndicatorBlinking(int index) {
	return blinking.active()[index];
}
static void clearKnobIndicatorLevels() {
	++clears.active();
}
static void setKnobIndicatorLevel(uint8_t index, int32_t level, bool bipolar) {
	auto& target = outputs.active()[index];
	++target.calls;
	target.level = level;
	target.bipolar = bipolar;
}
} // namespace indicator_leds
enum class MIDIFollowFeedbackAutomationMode { DISABLED, ENABLED };
static struct {
	bool active = false;
	bool isEitherClockActive() { return active; }
} playbackHandler;
static struct {
	MIDIFollowFeedbackAutomationMode midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
} midiEngine;
constexpr int kNoSelection = -1;
struct View {
	int feedback_calls = 0;
	bool renderedVUMeter = false;
	void setModLedStates() {
		if (on_mod_leds)
			on_mod_leds();
	}
	void setActiveModControllableWithoutTimelineCounter(ModControllable*, ParamManager*);
	void setActiveModControllableTimelineCounter(TimelineCounter*, bool);
	uint32_t modLength = 0;
	int32_t modNoteRowId = 0;
	void pretendModKnobsUntouchedForAWhile() {}
	void sendMidiFollowFeedback(ModelStackWithAutoParam* = nullptr, int32_t = 0, bool = false) { ++feedback_calls; }
	void setModRegion(uint32_t, uint32_t, int32_t);
	ModelStackWithAutoParam activeModControllableModelStack;
	uint32_t modPos = 0;
	void setKnobIndicatorLevel(uint8_t);
	void setKnobIndicatorLevels();
	int32_t convertPatchCableKnobPosToIndicatorLevel(int32_t);
};
static session::State<View> views;
static View& view_for_session() {
	return views.active();
}
#include "knob_indicator.inc"
#include "knob_indicator_batch.inc"
} // namespace knob_indicator_test
using namespace knob_indicator_test;
TEST_GROUP(KnobIndicator) {
	ModControllable controllable;
	ParamManager manager;
	ModelStackWithAutoParam stack;
	ParamCollection collection;
	AutoParam param;
	void setup() override {
		session::detail::active = session::Id::Local;
		views = {};
		root = {};
		automation = {};
		lookup_calls = 0;
		indicator_leds::blinking = {};
		indicator_leds::clears = {};
		roots.for_owner(session::Id::Local) = &root;
		roots.for_owner(session::Id::Remote) = &root;
		on_lookup = on_value = on_grab = on_mod_leds = on_redraw = on_resolve = on_activate = {};
		redraw_calls = 0;
		currentSong = &song;
		playbackHandler.active = false;
		midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
		indicator_leds::outputs = {};
		bipolar = quantized = stuttering = false;
		controllable.result = &stack;
		stack.modControllable = &controllable;
		for (auto owner : {session::Id::Local, session::Id::Remote})
			views.for_owner(owner).activeModControllableModelStack.modControllable = &controllable;
	}
	void teardown() override {
		on_lookup = on_value = on_grab = on_mod_leds = on_redraw = on_resolve = on_activate = {};
		redraw_calls = 0;
		session::detail::active = session::Id::Local;
	}
};
TEST(KnobIndicator, absent_controllable_or_mapping_does_not_dereference_target) {
	view_for_session().activeModControllableModelStack.modControllable = nullptr;
	view_for_session().setKnobIndicatorLevel(0);
	view_for_session().activeModControllableModelStack.modControllable = &controllable;
	controllable.result = nullptr;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
}
TEST(KnobIndicator, unavailable_plain_parameter_has_deterministic_off_level) {
	stack.paramId = 0xff10;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].level);
	stack.autoParam = &param;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].level);
}
TEST(KnobIndicator, current_values_and_automation_route_to_owning_panel) {
	stack.paramCollection = &collection;
	collection.has_value = true;
	collection.value = -32;
	view_for_session().setKnobIndicatorLevel(0);
	{
		session::Scope scope(session::Id::Remote);
		stack.autoParam = &param;
		param.value = 32;
		bipolar = true;
		view_for_session().setKnobIndicatorLevel(1);
		LONGS_EQUAL(96, indicator_leds::outputs.active()[1].level);
		CHECK(indicator_leds::outputs.active()[1].bipolar);
		LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
	}
	LONGS_EQUAL(32, indicator_leds::outputs.active()[0].level);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[1].calls);
}
TEST(KnobIndicator, nonexistent_and_patch_cable_defaults_are_preserved) {
	stack.paramId = 255;
	controllable.fallback = -16;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(48, indicator_leds::outputs.active()[0].level);
	stack.paramId = 0;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(64, indicator_leds::outputs.active()[0].level);
	CHECK(indicator_leds::outputs.active()[0].bipolar);
}
TEST(KnobIndicator, stutter_steps_and_patch_cable_scaling_are_preserved) {
	stack.paramCollection = &collection;
	collection.has_value = true;
	quantized = true;
	for (auto value : {-64, -32, 0, 32, 64}) {
		collection.value = value;
		view_for_session().setKnobIndicatorLevel(0);
		LONGS_EQUAL(value + 64, indicator_leds::outputs.active()[0].level);
	}
	stuttering = true;
	collection.value = -20;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(44, indicator_leds::outputs.active()[0].level);
	quantized = false;
	collection.kind = params::Kind::PATCH_CABLE;
	collection.value = -64;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(64, indicator_leds::outputs.active()[0].level);
}

TEST(KnobIndicator, missing_fallback_controllable_leaves_indicator_off) {
	stack.paramId = 255;
	stack.modControllable = nullptr;
	view_for_session().setKnobIndicatorLevel(0);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].level);
	CHECK_FALSE(indicator_leds::outputs.active()[0].bipolar);
}

TEST(KnobIndicator, lookup_context_changes_cancel_indicator_output) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int scenario = 0; scenario < 6; ++scenario) {
			auto& view = view_for_session();
			view.activeModControllableModelStack = {};
			view.activeModControllableModelStack.modControllable = &controllable;
			view.modPos = 0;
			currentSong = &song;
			on_lookup = [&] {
				if (scenario == 0)
					session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
				if (scenario == 1)
					currentSong = &replacement_song;
				if (scenario == 2)
					view.activeModControllableModelStack.modControllable = nullptr;
				if (scenario == 3)
					view.activeModControllableModelStack.paramManager = &manager;
				if (scenario == 4)
					view.activeModControllableModelStack.timeline = &song;
				if (scenario == 5)
					view.modPos = 48;
			};
			view.setKnobIndicatorLevel(0);
			CHECK(session::current() == owner);
			LONGS_EQUAL(0, indicator_leds::outputs.for_owner(owner)[0].calls);
		}
	}
}
TEST(KnobIndicator, value_callback_owner_change_does_not_send_to_peer) {
	stack.autoParam = &param;
	stack.paramCollection = &collection;
	on_value = [] { session::detail::active = session::Id::Remote; };
	view_for_session().setKnobIndicatorLevel(0);
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, indicator_leds::outputs.for_owner(session::Id::Remote)[0].calls);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
}

TEST(KnobIndicator, batch_stops_after_first_lookup_changes_context) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int scenario = 0; scenario < 5; ++scenario) {
			auto& view = view_for_session();
			view.activeModControllableModelStack = {};
			view.activeModControllableModelStack.modControllable = &controllable;
			view.modPos = 0;
			roots.active() = &root;
			currentSong = &song;
			lookup_calls = 0;
			on_lookup = [&] {
				if (scenario == 0)
					view.activeModControllableModelStack.paramManager = &manager;
				if (scenario == 1)
					currentSong = &replacement_song;
				if (scenario == 2)
					view.activeModControllableModelStack.timeline = &song;
				if (scenario == 3)
					view.modPos = 48;
				if (scenario == 4)
					roots.active() = &automation;
			};
			view.setKnobIndicatorLevels();
			LONGS_EQUAL(1, lookup_calls);
			LONGS_EQUAL(0, indicator_leds::outputs.active()[1].calls);
		}
	}
}

TEST(KnobIndicator, batch_preserves_blinking_and_delegates_automation_display) {
	indicator_leds::blinking.active()[0] = true;
	view_for_session().setKnobIndicatorLevels();
	LONGS_EQUAL(1, lookup_calls);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
	LONGS_EQUAL(1, indicator_leds::outputs.active()[1].calls);
	roots.active() = &automation;
	automation.automation_editor = true;
	view_for_session().setKnobIndicatorLevels();
	LONGS_EQUAL(1, automation.renders);
	LONGS_EQUAL(1, lookup_calls);
}
TEST(KnobIndicator, batch_handles_missing_root_and_clears_missing_target) {
	roots.active() = nullptr;
	view_for_session().setKnobIndicatorLevels();
	LONGS_EQUAL(0, lookup_calls);
	roots.active() = &root;
	view_for_session().activeModControllableModelStack.modControllable = nullptr;
	view_for_session().setKnobIndicatorLevels();
	LONGS_EQUAL(1, indicator_leds::clears.active());
}

TEST(KnobIndicator, region_value_grab_context_change_skips_indicators_and_feedback) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		currentSong = &song;
		auto& view = view_for_session();
		view.activeModControllableModelStack.paramManager = &manager;
		view.activeModControllableModelStack.timeline = &song;
		midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::ENABLED;
		on_grab = [] { currentSong = &replacement_song; };
		view.setModRegion(24, 12, 7);
		LONGS_EQUAL(0, lookup_calls);
		LONGS_EQUAL(0, view.feedback_calls);
		LONGS_EQUAL(24, view.modPos);
	}
}
TEST(KnobIndicator, region_selection_obeys_playback_and_manager_compatibility) {
	auto& view = view_for_session();
	view.activeModControllableModelStack.paramManager = &manager;
	view.activeModControllableModelStack.timeline = &song;
	midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::ENABLED;
	view.setModRegion(24, 12, 7);
	LONGS_EQUAL(1, manager.grabs);
	LONGS_EQUAL(1, view.feedback_calls);
	LONGS_EQUAL(12, view.modLength);
	LONGS_EQUAL(7, view.modNoteRowId);
	playbackHandler.active = true;
	view.setModRegion(48, 12, 8);
	LONGS_EQUAL(1, manager.grabs);
	playbackHandler.active = false;
	manager.compatible = false;
	view.setModRegion(72, 12, 9);
	LONGS_EQUAL(1, manager.grabs);
	midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
	view.setModRegion(0, 0, 0);
	LONGS_EQUAL(3, view.feedback_calls);
}

TEST(KnobIndicator, region_indicator_lookup_change_prevents_feedback) {
	auto& view = view_for_session();
	midiEngine.midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::ENABLED;
	on_lookup = [&] { view.modNoteRowId = 99; };
	view.setModRegion(24, 0, 7);
	LONGS_EQUAL(0, view.feedback_calls);
	LONGS_EQUAL(99, view.modNoteRowId);
	LONGS_EQUAL(1, lookup_calls);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[1].calls);
}
TEST(KnobIndicator, region_value_grab_owner_change_restores_initiating_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = view_for_session();
		view.activeModControllableModelStack.paramManager = &manager;
		view.activeModControllableModelStack.timeline = &song;
		on_grab = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		view.setModRegion(24, 12, 7);
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, lookup_calls);
		LONGS_EQUAL(0, view.feedback_calls);
	}
}

TEST(KnobIndicator, changed_region_length_cancels_pending_indicator_batch) {
	auto& view = view_for_session();
	on_lookup = [&] { view.modLength = 96; };
	view.setKnobIndicatorLevels();
	LONGS_EQUAL(1, lookup_calls);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[1].calls);
}

TEST(KnobIndicator, target_selection_stops_after_led_callback_changes_context) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = view_for_session();
		view.renderedVUMeter = true;
		on_mod_leds = [] { currentSong = &replacement_song; };
		currentSong = &song;
		view.setActiveModControllableWithoutTimelineCounter(&controllable, &manager);
		LONGS_EQUAL(0, lookup_calls);
		LONGS_EQUAL(0, redraw_calls);
		LONGS_EQUAL(0, view.feedback_calls);
	}
}
TEST(KnobIndicator, target_selection_updates_normal_indicators_sidebar_and_feedback) {
	auto& view = view_for_session();
	view.renderedVUMeter = true;
	view.setActiveModControllableWithoutTimelineCounter(&controllable, &manager);
	POINTERS_EQUAL(&controllable, view.activeModControllableModelStack.modControllable);
	POINTERS_EQUAL(&manager, view.activeModControllableModelStack.paramManager);
	POINTERS_EQUAL(nullptr, view.activeModControllableModelStack.timeline);
	LONGS_EQUAL(2, lookup_calls);
	LONGS_EQUAL(1, redraw_calls);
	LONGS_EQUAL(1, view.feedback_calls);
}

TEST(KnobIndicator, target_selection_owner_change_restores_caller_without_peer_feedback) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		on_mod_leds = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		view_for_session().setActiveModControllableWithoutTimelineCounter(&controllable, &manager);
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, view_for_session().feedback_calls);
		LONGS_EQUAL(0, lookup_calls);
	}
}
TEST(KnobIndicator, target_selection_cancels_feedback_after_sidebar_context_change) {
	auto& view = view_for_session();
	view.renderedVUMeter = true;
	on_redraw = [&] { view.modNoteRowId = 99; };
	view.setActiveModControllableWithoutTimelineCounter(&controllable, &manager);
	LONGS_EQUAL(1, redraw_calls);
	LONGS_EQUAL(0, view.feedback_calls);
	LONGS_EQUAL(99, view.modNoteRowId);
}
TEST(KnobIndicator, target_selection_preserves_replacement_model_during_indicator_lookup) {
	auto& view = view_for_session();
	on_lookup = [&] { view.activeModControllableModelStack.modControllable = nullptr; };
	view.setActiveModControllableWithoutTimelineCounter(&controllable, &manager);
	POINTERS_EQUAL(nullptr, view.activeModControllableModelStack.modControllable);
	LONGS_EQUAL(0, view.feedback_calls);
}

TEST(KnobIndicator, timeline_selection_preserves_context_changed_during_resolution) {
	TimelineCounter counter;
	counter.target = &controllable;
	counter.manager = &manager;
	auto& view = view_for_session();
	on_resolve = [] { currentSong = &replacement_song; };
	view.setActiveModControllableTimelineCounter(&counter, true);
	LONGS_EQUAL(0, counter.activations);
	LONGS_EQUAL(0, lookup_calls);
	LONGS_EQUAL(0, view.feedback_calls);
	POINTERS_EQUAL(nullptr, view.activeModControllableModelStack.timeline);
}
TEST(KnobIndicator, timeline_selection_uses_resolved_target_and_feedback_preference) {
	TimelineCounter requested, resolved;
	requested.redirected = &resolved;
	resolved.target = &controllable;
	resolved.manager = &manager;
	auto& view = view_for_session();
	view.setActiveModControllableTimelineCounter(&requested, false);
	POINTERS_EQUAL(&resolved, view.activeModControllableModelStack.timeline);
	POINTERS_EQUAL(&manager, view.activeModControllableModelStack.paramManager);
	LONGS_EQUAL(0, requested.activations);
	LONGS_EQUAL(1, resolved.activations);
	LONGS_EQUAL(0, view.feedback_calls);
	view.setActiveModControllableTimelineCounter(&requested, true);
	LONGS_EQUAL(1, view.feedback_calls);
}

TEST(KnobIndicator, timeline_resolution_owner_change_restores_both_panel_callers) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		TimelineCounter counter;
		on_resolve = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		view_for_session().setActiveModControllableTimelineCounter(&counter, true);
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, counter.activations);
		LONGS_EQUAL(0, view_for_session().feedback_calls);
	}
}
TEST(KnobIndicator, timeline_activation_context_change_stops_followup_rendering) {
	TimelineCounter counter;
	counter.target = &controllable;
	counter.manager = &manager;
	on_activate = [] { currentSong = &replacement_song; };
	view_for_session().setActiveModControllableTimelineCounter(&counter, true);
	LONGS_EQUAL(1, counter.activations);
	LONGS_EQUAL(0, lookup_calls);
	LONGS_EQUAL(0, view_for_session().feedback_calls);
}
TEST(KnobIndicator, timeline_selection_preserves_nested_selection_during_resolution) {
	TimelineCounter counter;
	auto& view = view_for_session();
	on_resolve = [&] { view.activeModControllableModelStack.modControllable = nullptr; };
	view.setActiveModControllableTimelineCounter(&counter, true);
	POINTERS_EQUAL(nullptr, view.activeModControllableModelStack.modControllable);
	LONGS_EQUAL(0, counter.activations);
}
TEST(KnobIndicator, timeline_null_resolution_clears_target_without_dereference) {
	TimelineCounter counter;
	counter.redirected = nullptr;
	auto& view = view_for_session();
	view.setActiveModControllableTimelineCounter(&counter, false);
	POINTERS_EQUAL(nullptr, view.activeModControllableModelStack.timeline);
	POINTERS_EQUAL(nullptr, view.activeModControllableModelStack.modControllable);
	POINTERS_EQUAL(nullptr, view.activeModControllableModelStack.paramManager);
	LONGS_EQUAL(1, indicator_leds::clears.active());
}
