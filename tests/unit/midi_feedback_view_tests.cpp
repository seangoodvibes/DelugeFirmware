#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <array>
#include <cstdint>
namespace midi_feedback_view_test {
namespace session = ::deluge::gui::ui_session;
namespace params {
enum class Kind { NORMAL };
}
struct ParamCollection {
	params::Kind getParamKind() { return params::Kind::NORMAL; }
};
struct ModelStackWithAutoParam {
	void* autoParam = nullptr;
	ParamCollection* paramCollection = nullptr;
	int32_t paramId = 0;
};
struct feedback_fixture {
	bool enabled = true;
	int cc = 7, lookups = 0, param_id = -1;
	struct output {
		int sends = 0, fallback = 0, cc = -1, value = 0;
		bool automation = false;
	};
	session::State<output> outputs;
	bool isFeedbackEnabled() { return enabled; }
	int getCCFromParam(params::Kind, int id) {
		++lookups;
		param_id = id;
		return cc;
	}
	void sendCCForMidiFollowFeedback(int number, int value) {
		auto& out = outputs.active();
		++out.sends;
		out.cc = number;
		out.value = value;
	}
	void sendCCWithoutModelStackForMidiFollowFeedback(bool automation) {
		auto& out = outputs.active();
		++out.fallback;
		out.automation = automation;
	}
};
static feedback_fixture midiFollow;
struct ParamManager {};
struct editor_fixture {
	ParamManager* currentParamManager = nullptr;
};
static session::State<editor_fixture> editors;
static session::State<editor_fixture*> current_uis;
static editor_fixture& sound_editor_for_session() {
	return editors.active();
}
static editor_fixture* getCurrentUI() {
	return current_uis.active();
}
enum class TimerName { DISPLAY_AUTOMATION, SEND_MIDI_FEEDBACK_FOR_AUTOMATION };
struct timer_fixture {
	struct state {
		bool set = false;
		int calls = 0, delay = 0;
	};
	session::State<std::array<state, 2>> timers;
	bool isTimerSet(TimerName name) { return timers.active()[static_cast<int>(name)].set; }
	void setTimer(TimerName name, int delay) {
		auto& target = timers.active()[static_cast<int>(name)];
		target.set = true;
		++target.calls;
		target.delay = delay;
	}
};
static timer_fixture uiTimerManager;
struct View {
	struct {
		ParamManager* paramManager = nullptr;
	} activeModControllableModelStack;
	bool pendingParamAutomationUpdatesModLevels = false;
	void notifyParamAutomationOccurred(ParamManager*, bool);
	bool clip_context = true;
	bool isClipContext() { return clip_context; }
	void sendMidiFollowFeedback(ModelStackWithAutoParam*, int32_t, bool);
};
#include "midi_feedback_view.inc"
} // namespace midi_feedback_view_test
using namespace midi_feedback_view_test;
TEST_GROUP(MidiFeedbackView) {
	View view;
	ParamCollection collection;
	ModelStackWithAutoParam stack;
	int param = 0;
	void setup() override {
		session::detail::active = session::Id::Local;
		midiFollow = {};
		editors = {};
		current_uis = {};
		uiTimerManager = {};
		stack.autoParam = &param;
		stack.paramCollection = &collection;
		stack.paramId = 11;
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(MidiFeedbackView, incomplete_parameter_mapping_sends_no_feedback) {
	stack.paramCollection = nullptr;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		view.sendMidiFollowFeedback(&stack, 31, true);
		LONGS_EQUAL(0, midiFollow.outputs.active().sends);
		LONGS_EQUAL(0, midiFollow.outputs.active().fallback);
	}
	LONGS_EQUAL(0, midiFollow.lookups);
}
TEST(MidiFeedbackView, valid_mapping_keeps_cc_value_and_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		view.sendMidiFollowFeedback(&stack, 31, true);
		LONGS_EQUAL(1, midiFollow.outputs.active().sends);
		LONGS_EQUAL(7, midiFollow.outputs.active().cc);
		LONGS_EQUAL(31, midiFollow.outputs.active().value);
		LONGS_EQUAL(11, midiFollow.param_id);
	}
}
TEST(MidiFeedbackView, disabled_song_and_unmapped_parameters_do_not_send) {
	midiFollow.enabled = false;
	view.sendMidiFollowFeedback(&stack, 0, false);
	midiFollow.enabled = true;
	view.clip_context = false;
	view.sendMidiFollowFeedback(&stack, 0, false);
	LONGS_EQUAL(0, midiFollow.lookups);
	view.clip_context = true;
	midiFollow.cc = MIDI_CC_NONE;
	view.sendMidiFollowFeedback(&stack, 0, false);
	LONGS_EQUAL(1, midiFollow.lookups);
	LONGS_EQUAL(0, midiFollow.outputs.active().sends);
	LONGS_EQUAL(0, midiFollow.outputs.active().fallback);
}
TEST(MidiFeedbackView, absent_parameter_preserves_automation_fallback) {
	view.sendMidiFollowFeedback(nullptr, 0, true);
	LONGS_EQUAL(1, midiFollow.outputs.active().fallback);
	CHECK(midiFollow.outputs.active().automation);
	stack.autoParam = nullptr;
	stack.paramCollection = nullptr;
	view.sendMidiFollowFeedback(&stack, 0, false);
	LONGS_EQUAL(2, midiFollow.outputs.active().fallback);
	CHECK_FALSE(midiFollow.outputs.active().automation);
	LONGS_EQUAL(0, midiFollow.lookups);
}

TEST(MidiFeedbackView, null_notification_does_not_match_unbound_view_or_editor) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		current_uis.active() = &editors.active();
		view.notifyParamAutomationOccurred(nullptr, true);
		for (auto& timer : uiTimerManager.timers.active())
			LONGS_EQUAL(0, timer.calls);
		CHECK_FALSE(view.pendingParamAutomationUpdatesModLevels);
	}
}
TEST(MidiFeedbackView, automation_notifications_coalesce_and_promote_level_updates) {
	ParamManager manager;
	view.activeModControllableModelStack.paramManager = &manager;
	view.notifyParamAutomationOccurred(&manager, false);
	CHECK_FALSE(view.pendingParamAutomationUpdatesModLevels);
	view.notifyParamAutomationOccurred(&manager, true);
	view.notifyParamAutomationOccurred(&manager, false);
	CHECK(view.pendingParamAutomationUpdatesModLevels);
	for (auto& timer : uiTimerManager.timers.active()) {
		LONGS_EQUAL(1, timer.calls);
		LONGS_EQUAL(25, timer.delay);
	}
	uiTimerManager.timers.active()[0].set = false;
	view.notifyParamAutomationOccurred(&manager, false);
	CHECK_FALSE(view.pendingParamAutomationUpdatesModLevels);
	LONGS_EQUAL(2, uiTimerManager.timers.active()[0].calls);
	LONGS_EQUAL(1, uiTimerManager.timers.active()[1].calls);
}
TEST(MidiFeedbackView, only_visible_editor_manager_schedules_its_panel) {
	ParamManager manager, unrelated;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		editors.active().currentParamManager = &manager;
		view.notifyParamAutomationOccurred(&manager, true);
		LONGS_EQUAL(0, uiTimerManager.timers.active()[0].calls);
		current_uis.active() = &editors.active();
		view.notifyParamAutomationOccurred(&unrelated, true);
		LONGS_EQUAL(0, uiTimerManager.timers.active()[0].calls);
		view.notifyParamAutomationOccurred(&manager, true);
		LONGS_EQUAL(1, uiTimerManager.timers.active()[0].calls);
		LONGS_EQUAL(1, uiTimerManager.timers.active()[1].calls);
	}
}
