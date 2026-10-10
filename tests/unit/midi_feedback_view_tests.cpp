#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <cstdint>
namespace midi_feedback_view_test {
namespace session = ::deluge::gui::ui_session;
namespace params {
enum class Kind { NORMAL };
}
constexpr int MIDI_CC_NONE = -1;
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
struct View {
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
