#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
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
static std::function<void()> on_has_value, on_current_value, on_kind, on_conversion, on_fallback;
static int conversion_calls = 0;
static std::function<void()> on_encoder, on_mark_edited;
static bool encoder_edited = false;
constexpr int NUM_LEVEL_INDICATORS = 2;
static int lookup_calls = 0;
struct root_fixture {
	root_fixture* menu = this;
	int reads = 0;
	root_fixture* getCurrentMenuItem() { return menu; }
	void readValueAgain() { ++reads; }
	bool onArrangerView = false;
	bool automation_editor = false;
	int renders = 0;
	bool inAutomationEditor() { return automation_editor; }
	void displayAutomation() { ++renders; }
};
static root_fixture root, automation, editor;
static session::State<root_fixture*> current_uis;
static root_fixture* getCurrentUI() {
	return current_uis.active();
}
static root_fixture& sound_editor_for_session() {
	return editor;
}
static session::State<root_fixture*> roots;
static root_fixture* getRootUI() {
	return roots.active();
}
static root_fixture& automation_view_for_session() {
	return automation;
}
struct song_fixture {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
static song_fixture song, replacement_song;
static song_fixture* currentSong = &song;
struct ModelStackWithAutoParam;
struct ParamCollection {
	bool has_value = false;
	int32_t value = 0;
	params::Kind kind = params::Kind::NORMAL;
	bool has_current_value(int32_t) {
		if (on_has_value)
			on_has_value();
		return has_value;
	}
	int32_t get_current_value(int32_t) {
		if (on_current_value)
			on_current_value();
		return value;
	}
	params::Kind getParamKind() {
		if (on_kind)
			on_kind();
		return kind;
	}
	int32_t paramValueToKnobPos(int32_t value, ModelStackWithAutoParam*) {
		++conversion_calls;
		if (on_conversion)
			on_conversion();
		return value;
	}
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
using ParamManagerForTimeline = ParamManager;
struct ModControllable {
	uint8_t mode = 0;
	bool missing_mode = false;
	int presses = 0, releases = 0;
	std::function<void()> on_button;
	std::function<void()> on_mode;
	uint8_t* getModKnobMode() {
		if (on_mode)
			on_mode();
		return missing_mode ? nullptr : &mode;
	}
	void modButtonAction(uint8_t, bool on, ParamManager*) {
		if (on)
			++presses;
		else
			++releases;
		if (on_button)
			on_button();
	}
	template <class T>
	bool modEncoderButtonAction(uint8_t, bool, T*) {
		if (on_encoder)
			on_encoder();
		return encoder_edited;
	}
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
	int32_t getKnobPosForNonExistentParam(uint8_t, ModelStackWithAutoParam*) {
		if (on_fallback)
			on_fallback();
		return fallback;
	}
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
	song_fixture* song = nullptr;
	void* getTimelineCounterAllowNull() const { return timeline; }
	bool timelineCounterIsSet() const { return timeline != nullptr; }
	ModControllable* modControllable = nullptr;
	AutoParam* autoParam = nullptr;
	ParamCollection* paramCollection = nullptr;
	int32_t paramId = 0;
};
using ModelStackWithTimelineCounter = ModelStackWithAutoParam;
using ModelStackWithThreeMainThings = ModelStackWithAutoParam;
constexpr size_t MODEL_STACK_MAX_SIZE = sizeof(ModelStackWithAutoParam);
static void copyModelStack(void* destination, const void* source, size_t size) {
	std::memcpy(destination, source, size);
}
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
static ModelStackWithAutoParam* setupModelStackWithSong(ModelStackWithAutoParam* stack, song_fixture* song) {
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
constexpr int kDisplayHeight = 8, kDisplayWidth = 16, kSideBarWidth = 2;
struct RGB {
	int value = 0;
};
namespace colours {
static const RGB black{};
}
namespace PadLEDs {
static session::State<bool> rendering_locks;
static bool& rendering_lock_for_session() {
	return rendering_locks.active();
}
} // namespace PadLEDs
namespace AudioEngine {
static struct {
	float l = 0, r = 0;
} approxRMSLevel;
} // namespace AudioEngine
struct View {
	bool clip_context = false;
	int cachedMaxYDisplayForVUMeterL = 255, cachedMaxYDisplayForVUMeterR = 255;
	int vu_renders = 0;
	bool isClipContext() { return clip_context; }
	int getMaxYDisplayForVUMeter(float) { return 1; }
	void renderVUMeter(int, int, RGB[][kDisplayWidth + kSideBarWidth]) { ++vu_renders; }
	bool potentiallyRenderVUMeter(RGB[][kDisplayWidth + kSideBarWidth]);
	int feedback_calls = 0;
	int edits = 0;
	void instrumentBeenEdited() {
		++edits;
		if (on_mark_edited)
			on_mark_edited();
	}
	void modEncoderButtonAction_changeModControllable(uint8_t, bool);
	bool renderedVUMeter = false;
	bool displayVUMeter = false;
	void modButtonAction(uint8_t, bool);
	int32_t getModKnobMode();
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
using RootUI = root_fixture;
static const uint32_t modButtonUIModes[] = {0};
static bool isUIModeWithinRange(const uint32_t*) {
	return true;
}
static root_fixture& performance_view_for_session() {
	return root;
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
		editor.menu = &editor;
		editor.reads = 0;
		current_uis.for_owner(session::Id::Local) = &editor;
		current_uis.for_owner(session::Id::Remote) = &editor;
		root = {};
		automation = {};
		lookup_calls = 0;
		indicator_leds::blinking = {};
		indicator_leds::clears = {};
		roots.for_owner(session::Id::Local) = &root;
		roots.for_owner(session::Id::Remote) = &root;
		on_lookup = on_value = on_grab = on_mod_leds = on_redraw = on_resolve = on_activate = {};
		redraw_calls = 0;
		on_has_value = on_current_value = on_kind = on_conversion = on_fallback = {};
		conversion_calls = 0;
		on_encoder = on_mark_edited = {};
		encoder_edited = false;
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
		on_has_value = on_current_value = on_kind = on_conversion = on_fallback = {};
		conversion_calls = 0;
		on_encoder = on_mark_edited = {};
		encoder_edited = false;
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

TEST(KnobIndicator, parameter_mapping_changes_cancel_value_pipeline) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int phase = 0; phase < 5; ++phase) {
			stack.paramCollection = &collection;
			stack.autoParam = phase == 2 ? &param : nullptr;
			stack.paramId = 10;
			collection.has_value = true;
			conversion_calls = 0;
			on_has_value = on_current_value = on_value = on_kind = on_conversion = {};
			auto replace_mapping = [&] { stack.paramId = 11; };
			if (phase == 0)
				on_has_value = replace_mapping;
			if (phase == 1)
				on_current_value = replace_mapping;
			if (phase == 2)
				on_value = replace_mapping;
			if (phase == 3)
				on_kind = replace_mapping;
			if (phase == 4)
				on_conversion = replace_mapping;
			view_for_session().setKnobIndicatorLevel(0);
			LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
			LONGS_EQUAL(phase == 4 ? 1 : 0, conversion_calls);
		}
	}
}
TEST(KnobIndicator, value_callback_cannot_mix_collections_or_auto_parameters) {
	ParamCollection replacement_collection;
	AutoParam replacement_param;
	for (int scenario = 0; scenario < 3; ++scenario) {
		stack.paramCollection = &collection;
		stack.autoParam = &param;
		stack.modControllable = &controllable;
		on_value = [&] {
			if (scenario == 0)
				stack.paramCollection = &replacement_collection;
			if (scenario == 1)
				stack.autoParam = &replacement_param;
			if (scenario == 2)
				stack.modControllable = nullptr;
		};
		view_for_session().setKnobIndicatorLevel(0);
		LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
		LONGS_EQUAL(0, conversion_calls);
	}
}

TEST(KnobIndicator, legacy_fallback_mapping_change_cancels_output_on_both_panels) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		stack.paramId = 255;
		on_fallback = [&] { stack.paramId = 10; };
		view_for_session().setKnobIndicatorLevel(0);
		LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
	}
}
TEST(KnobIndicator, timeline_activation_preserves_replaced_timeline) {
	TimelineCounter counter, replacement;
	counter.target = &controllable;
	counter.manager = &manager;
	auto& view = view_for_session();
	on_activate = [&] { view.activeModControllableModelStack.timeline = &replacement; };
	view.setActiveModControllableTimelineCounter(&counter, true);
	POINTERS_EQUAL(&replacement, view.activeModControllableModelStack.timeline);
	LONGS_EQUAL(0, lookup_calls);
	LONGS_EQUAL(0, view.feedback_calls);
}
TEST(KnobIndicator, timeline_followup_changes_stop_feedback_at_each_stage) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int phase = 0; phase < 3; ++phase) {
			TimelineCounter counter;
			counter.target = &controllable;
			counter.manager = &manager;
			auto& view = view_for_session();
			view.renderedVUMeter = true;
			view.modNoteRowId = 0;
			currentSong = &song;
			lookup_calls = 0;
			on_mod_leds = on_lookup = on_redraw = {};
			if (phase == 0)
				on_mod_leds = [] { currentSong = &replacement_song; };
			if (phase == 1)
				on_lookup = [&] { view.activeModControllableModelStack.paramManager = nullptr; };
			if (phase == 2)
				on_redraw = [&] { view.modNoteRowId = 99; };
			view.setActiveModControllableTimelineCounter(&counter, true);
			LONGS_EQUAL(phase, lookup_calls);
			LONGS_EQUAL(0, view.feedback_calls);
			CHECK(session::current() == owner);
		}
	}
}

TEST(KnobIndicator, encoder_callback_cannot_mark_or_render_replaced_song) {
	encoder_edited = true;
	on_encoder = [] { currentSong = &replacement_song; };
	view_for_session().modEncoderButtonAction_changeModControllable(0, true);
	LONGS_EQUAL(0, view_for_session().edits);
	LONGS_EQUAL(0, lookup_calls);
	LONGS_EQUAL(0, editor.reads);
}
TEST(KnobIndicator, encoder_normal_edit_and_no_edit_paths_refresh_menu) {
	auto& view = view_for_session();
	view.modEncoderButtonAction_changeModControllable(0, true);
	LONGS_EQUAL(0, view.edits);
	LONGS_EQUAL(1, editor.reads);
	encoder_edited = true;
	view.modEncoderButtonAction_changeModControllable(0, false);
	LONGS_EQUAL(1, view.edits);
	LONGS_EQUAL(2, editor.reads);
}

TEST(KnobIndicator, encoder_owner_change_restores_panel_without_followup) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		encoder_edited = true;
		on_encoder = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		view_for_session().modEncoderButtonAction_changeModControllable(0, true);
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, view_for_session().edits);
		LONGS_EQUAL(0, editor.reads);
	}
}
TEST(KnobIndicator, encoder_missing_targets_and_menus_are_safe) {
	auto& view = view_for_session();
	view.activeModControllableModelStack.modControllable = nullptr;
	view.modEncoderButtonAction_changeModControllable(0, true);
	LONGS_EQUAL(0, lookup_calls);
	view.activeModControllableModelStack.modControllable = &controllable;
	editor.menu = nullptr;
	view.modEncoderButtonAction_changeModControllable(0, true);
	LONGS_EQUAL(2, lookup_calls);
	LONGS_EQUAL(0, editor.reads);
}
TEST(KnobIndicator, encoder_menu_replacement_is_not_reread) {
	root_fixture replacement_menu;
	on_encoder = [&] { editor.menu = &replacement_menu; };
	view_for_session().modEncoderButtonAction_changeModControllable(0, true);
	LONGS_EQUAL(0, replacement_menu.reads);
	LONGS_EQUAL(0, editor.reads);
}
TEST(KnobIndicator, encoder_edit_notification_change_stops_indicator_update) {
	encoder_edited = true;
	on_mark_edited = [] { currentSong = &replacement_song; };
	view_for_session().modEncoderButtonAction_changeModControllable(0, true);
	LONGS_EQUAL(1, view_for_session().edits);
	LONGS_EQUAL(0, lookup_calls);
	LONGS_EQUAL(0, editor.reads);
}

TEST(KnobIndicator, mod_button_missing_mode_still_delivers_release) {
	controllable.missing_mode = true;
	view_for_session().modButtonAction(1, true);
	LONGS_EQUAL(0, controllable.presses);
	view_for_session().modButtonAction(1, false);
	LONGS_EQUAL(1, controllable.releases);
}
TEST(KnobIndicator, mod_button_sidebar_context_change_prevents_selection) {
	auto& view = view_for_session();
	view.renderedVUMeter = true;
	on_redraw = [] { currentSong = &replacement_song; };
	view.modButtonAction(1, true);
	LONGS_EQUAL(0, controllable.mode);
	LONGS_EQUAL(0, controllable.presses);
}
TEST(KnobIndicator, mod_button_callback_owner_change_stops_followup) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		controllable.on_button = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		view_for_session().modButtonAction(1, true);
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, lookup_calls);
	}
	controllable.on_button = {};
}
TEST(KnobIndicator, mod_button_indicator_change_prevents_led_followup) {
	int led_calls = 0;
	on_mod_leds = [&] { ++led_calls; };
	on_lookup = [] { currentSong = &replacement_song; };
	view_for_session().modButtonAction(1, true);
	LONGS_EQUAL(1, controllable.presses);
	LONGS_EQUAL(0, led_calls);
}
TEST(KnobIndicator, mod_button_normal_selection_and_vu_toggle) {
	auto& view = view_for_session();
	view.modButtonAction(0, true);
	CHECK(view.displayVUMeter);
	view.modButtonAction(1, true);
	LONGS_EQUAL(1, controllable.mode);
	LONGS_EQUAL(2, controllable.presses);
	LONGS_EQUAL(4, lookup_calls);
}

TEST(KnobIndicator, mod_button_target_changes_stop_followup_on_both_panels) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = view_for_session();
		for (int change = 0; change < 8; ++change) {
			view = View{};
			view.activeModControllableModelStack.modControllable = &controllable;
			currentSong = &song;
			roots.active() = &root;
			current_uis.active() = &editor;
			lookup_calls = 0;
			controllable.on_button = [&] {
				switch (change) {
				case 0:
					currentSong = &replacement_song;
					break;
				case 1:
					roots.active() = &automation;
					break;
				case 2:
					current_uis.active() = &root;
					break;
				case 3:
					view.activeModControllableModelStack.modControllable = nullptr;
					break;
				case 4:
					view.activeModControllableModelStack.paramManager = &manager;
					break;
				case 5:
					view.activeModControllableModelStack.timeline = &song;
					break;
				case 6:
					++view.modLength;
					break;
				case 7:
					++view.modNoteRowId;
					break;
				}
			};
			view.modButtonAction(1, true);
			LONGS_EQUAL(0, lookup_calls);
		}
		controllable.on_button = {};
	}
}
TEST(KnobIndicator, mod_button_automation_editor_preserves_arranger_exception) {
	roots.active() = &automation;
	automation.automation_editor = true;
	automation.onArrangerView = false;
	view_for_session().modButtonAction(0, true);
	LONGS_EQUAL(0, controllable.presses);
	automation.onArrangerView = true;
	view_for_session().modButtonAction(1, true);
	LONGS_EQUAL(0, controllable.presses);
	view_for_session().modButtonAction(0, true);
	LONGS_EQUAL(1, controllable.presses);
}

TEST(KnobIndicator, encoder_indicator_completion_cannot_refresh_changed_menu_context) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = view_for_session();
		for (int change = 0; change < 9; ++change) {
			view = View{};
			view.activeModControllableModelStack.modControllable = &controllable;
			currentSong = &song;
			roots.active() = &root;
			current_uis.active() = &editor;
			editor.menu = &editor;
			editor.reads = 0;
			on_lookup = [&] {
				switch (change) {
				case 0:
					currentSong = &replacement_song;
					break;
				case 1:
					roots.active() = &automation;
					break;
				case 2:
					current_uis.active() = &root;
					break;
				case 3:
					view.activeModControllableModelStack.modControllable = nullptr;
					break;
				case 4:
					view.activeModControllableModelStack.paramManager = &manager;
					break;
				case 5:
					view.activeModControllableModelStack.timeline = &song;
					break;
				case 6:
					++view.modPos;
					break;
				case 7:
					editor.menu = nullptr;
					break;
				case 8:
					session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
					break;
				}
			};
			view.modEncoderButtonAction_changeModControllable(0, true);
			CHECK(session::current() == owner);
			// Nested indicator scopes restore a temporary owner change. The original
			// menu remains valid in that case; persistent context changes cancel it.
			LONGS_EQUAL(change == 8 ? 1 : 0, editor.reads);
		}
		on_lookup = {};
	}
}

TEST(KnobIndicator, mod_button_mode_lookup_cannot_redirect_press) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		controllable.on_mode = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		view_for_session().modButtonAction(1, true);
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, controllable.presses);
		LONGS_EQUAL(0, controllable.mode);
	}
	controllable.on_mode = {};
}

TEST(KnobIndicator, mod_mode_lookup_rejects_replaced_controller) {
	auto& view = view_for_session();
	controllable.mode = 3;
	controllable.on_mode = [&] { view.activeModControllableModelStack.modControllable = nullptr; };
	LONGS_EQUAL(-1, view.getModKnobMode());
	controllable.on_mode = {};
}
TEST(KnobIndicator, mod_mode_lookup_restores_owner_and_rejects_changed_song) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		controllable.mode = 3;
		controllable.on_mode = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		LONGS_EQUAL(-1, view_for_session().getModKnobMode());
		CHECK(session::current() == owner);
		currentSong = &song;
		controllable.on_mode = [] { currentSong = &replacement_song; };
		LONGS_EQUAL(-1, view_for_session().getModKnobMode());
		controllable.on_mode = {};
	}
}
TEST(KnobIndicator, mod_mode_lookup_preserves_normal_and_missing_modes) {
	auto& view = view_for_session();
	controllable.mode = 3;
	LONGS_EQUAL(3, view.getModKnobMode());
	controllable.missing_mode = true;
	LONGS_EQUAL(-1, view.getModKnobMode());
	view.activeModControllableModelStack.modControllable = nullptr;
	LONGS_EQUAL(-1, view.getModKnobMode());
}

TEST(KnobIndicator, vu_meter_missing_mode_leaves_sidebar_available) {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = view_for_session();
		view.displayVUMeter = true;
		controllable.missing_mode = true;
		CHECK_FALSE(view.potentiallyRenderVUMeter(image));
		CHECK_FALSE(view.renderedVUMeter);
		LONGS_EQUAL(0, view.vu_renders);
		CHECK_FALSE(PadLEDs::rendering_lock_for_session());
	}
}
TEST(KnobIndicator, vu_meter_preserves_selected_clip_and_volume_mode_rendering) {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
	auto& view = view_for_session();
	view.displayVUMeter = true;
	CHECK(view.potentiallyRenderVUMeter(image));
	LONGS_EQUAL(2, view.vu_renders);
	CHECK(view.renderedVUMeter);
	CHECK_FALSE(PadLEDs::rendering_lock_for_session());
	controllable.missing_mode = true;
	view.clip_context = true;
	CHECK(view.potentiallyRenderVUMeter(image));
	LONGS_EQUAL(2, view.vu_renders);
	view.displayVUMeter = false;
	CHECK_FALSE(view.potentiallyRenderVUMeter(image));
	CHECK_FALSE(view.renderedVUMeter);
}

TEST(KnobIndicator, mod_mode_lookup_rejects_changed_manager_or_timeline_on_both_panels) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = view_for_session();
		for (bool change_manager : {false, true}) {
			view.activeModControllableModelStack.paramManager = nullptr;
			view.activeModControllableModelStack.timeline = nullptr;
			controllable.mode = 3;
			controllable.on_mode = [&] {
				if (change_manager)
					view.activeModControllableModelStack.paramManager = &manager;
				else
					view.activeModControllableModelStack.timeline = &song;
			};
			LONGS_EQUAL(-1, view.getModKnobMode());
			CHECK(session::current() == owner);
		}
		controllable.on_mode = {};
	}
}

TEST(KnobIndicator, vu_meter_without_volume_selection_returns_sidebar_to_caller) {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = view_for_session();
		view.displayVUMeter = true;
		view.renderedVUMeter = true;
		controllable.mode = 1;
		CHECK_FALSE(view.potentiallyRenderVUMeter(image));
		CHECK_FALSE(view.renderedVUMeter);
		view.activeModControllableModelStack.modControllable = nullptr;
		CHECK_FALSE(view.potentiallyRenderVUMeter(image));
		LONGS_EQUAL(0, view.vu_renders);
		CHECK_FALSE(PadLEDs::rendering_lock_for_session());
	}
}

TEST(KnobIndicator, song_reuse_during_indicator_lookup_cancels_publication) {
	on_lookup = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	view_for_session().setKnobIndicatorLevels();
	LONGS_EQUAL(1, lookup_calls);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[0].calls);
	LONGS_EQUAL(0, indicator_leds::outputs.active()[1].calls);
}
TEST(KnobIndicator, song_reuse_during_encoder_callback_cancels_followups) {
	encoder_edited = true;
	on_encoder = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	view_for_session().modEncoderButtonAction_changeModControllable(0, true);
	LONGS_EQUAL(0, view_for_session().edits);
	LONGS_EQUAL(0, lookup_calls);
	LONGS_EQUAL(0, editor.reads);
}
TEST(KnobIndicator, song_reuse_during_mod_button_callback_cancels_indicators) {
	controllable.on_button = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	view_for_session().modButtonAction(1, true);
	LONGS_EQUAL(1, controllable.presses);
	LONGS_EQUAL(0, lookup_calls);
	controllable.on_button = {};
}
TEST(KnobIndicator, song_reuse_during_mode_lookup_rejects_returned_pointer) {
	controllable.on_mode = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	LONGS_EQUAL(-1, view_for_session().getModKnobMode());
	controllable.on_mode = {};
}
TEST(KnobIndicator, song_reuse_during_timeline_resolution_cancels_selection) {
	TimelineCounter counter;
	counter.target = &controllable;
	counter.manager = &manager;
	on_resolve = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	view_for_session().setActiveModControllableTimelineCounter(&counter, true);
	LONGS_EQUAL(0, counter.activations);
	LONGS_EQUAL(0, view_for_session().feedback_calls);
}
TEST(KnobIndicator, song_reuse_during_target_leds_cancels_feedback) {
	on_mod_leds = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	view_for_session().setActiveModControllableWithoutTimelineCounter(&controllable, &manager);
	LONGS_EQUAL(0, lookup_calls);
	LONGS_EQUAL(0, view_for_session().feedback_calls);
}
TEST(KnobIndicator, song_reuse_during_region_indicators_cancels_feedback) {
	on_lookup = [] {
		song.~song_fixture();
		new (&song) song_fixture;
	};
	view_for_session().setModRegion(24, 0, 7);
	LONGS_EQUAL(1, lookup_calls);
	LONGS_EQUAL(0, view_for_session().feedback_calls);
}
TEST(KnobIndicator, retired_song_rejects_modulation_entry_points) {
	song_fixture retiring_song;
	retiring_song.lifetime.retire();
	currentSong = &retiring_song;
	auto& view = view_for_session();
	view.setKnobIndicatorLevels();
	view.setKnobIndicatorLevel(0);
	view.modEncoderButtonAction_changeModControllable(0, true);
	view.modButtonAction(1, true);
	LONGS_EQUAL(-1, view.getModKnobMode());
	view.setActiveModControllableWithoutTimelineCounter(&controllable, &manager);
	TimelineCounter counter;
	view.setActiveModControllableTimelineCounter(&counter, true);
	view.setModRegion(24, 0, 7);
	LONGS_EQUAL(0, lookup_calls);
	LONGS_EQUAL(0, controllable.presses);
	LONGS_EQUAL(0, counter.activations);
	LONGS_EQUAL(0, view.feedback_calls);
	currentSong = &song;
}
