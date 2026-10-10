#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <array>
namespace output_clip_led_test {
namespace session = ::deluge::gui::ui_session;
struct Output {
	OutputType type = OutputType::SYNTH;
};
struct Clip {
	ClipType type = ClipType::AUDIO;
};
struct InstrumentClip : Clip {
	InstrumentClip() { type = ClipType::INSTRUMENT; }
	Output* output = nullptr;
	bool inScaleMode = false;
	session::State<bool> keyboard, wrap;
	bool on_keyboard_screen_for_session() { return keyboard.active(); }
	bool wrap_editing_for_session() { return wrap.active(); }
};
namespace indicator_leds {
enum class LED { KEYBOARD, SCALE_MODE, CROSS_SCREEN_EDIT };
static session::State<std::array<bool, 3>> states;
static void setLedState(LED led, bool value) {
	states.active()[static_cast<size_t>(led)] = value;
}
} // namespace indicator_leds
#include "output_clip_leds.inc"
} // namespace output_clip_led_test
using namespace output_clip_led_test;
TEST_GROUP(OutputClipLEDs) {
	Output output;
	InstrumentClip clip;
	void setup() override {
		session::detail::active = session::Id::Local;
		indicator_leds::states = {};
		clip.output = &output;
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(OutputClipLEDs, instrument_clip_modes_light_the_selected_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		clip.keyboard.active() = true;
		clip.wrap.active() = true;
		clip.inScaleMode = true;
		set_output_clip_led_states(&clip);
		CHECK(indicator_leds::states.active()[0]);
		CHECK(indicator_leds::states.active()[1]);
		CHECK(indicator_leds::states.active()[2]);
	}
}
TEST(OutputClipLEDs, panel_navigation_is_independent_with_shared_scale_state) {
	clip.inScaleMode = true;
	clip.keyboard.for_owner(session::Id::Local) = true;
	clip.wrap.for_owner(session::Id::Remote) = true;
	set_output_clip_led_states(&clip);
	{
		session::Scope scope(session::Id::Remote);
		set_output_clip_led_states(&clip);
		CHECK_FALSE(indicator_leds::states.active()[0]);
		CHECK(indicator_leds::states.active()[1]);
		CHECK(indicator_leds::states.active()[2]);
	}
	CHECK(indicator_leds::states.active()[0]);
	CHECK(indicator_leds::states.active()[1]);
	CHECK_FALSE(indicator_leds::states.active()[2]);
}
TEST(OutputClipLEDs, kit_or_missing_output_does_not_light_scale_mode) {
	clip.inScaleMode = true;
	output.type = OutputType::KIT;
	set_output_clip_led_states(&clip);
	CHECK_FALSE(indicator_leds::states.active()[1]);
	clip.output = nullptr;
	set_output_clip_led_states(&clip);
	CHECK_FALSE(indicator_leds::states.active()[1]);
}
TEST(OutputClipLEDs, absent_and_audio_clips_clear_all_three_leds) {
	Clip audio_clip;
	for (auto* target : {static_cast<Clip*>(nullptr), &audio_clip}) {
		indicator_leds::states.active() = {true, true, true};
		set_output_clip_led_states(target);
		for (bool lit : indicator_leds::states.active())
			CHECK_FALSE(lit);
	}
}
