#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui_session.h"
#include <array>
namespace mod_led_test {
namespace session = ::deluge::gui::ui_session;
enum class RecordingMode { OFF, ARRANGEMENT };
enum class IndicatorLED { AFFECT_ENTIRE, CLIP_VIEW, SESSION_VIEW, MOD_0 };
struct Output {};
struct Clip {
	ClipType type = ClipType::AUDIO;
	session::State<bool> automation;
	bool on_automation_clip_view_for_session() { return automation.active(); }
};
struct InstrumentClip : Clip {
	InstrumentClip() { type = ClipType::INSTRUMENT; }
	session::State<bool> affect;
	bool affect_entire_for_session() { return affect.active(); }
};
struct RootUI {
	UIType type = UIType::NONE, context = UIType::NONE;
	bool onArrangerView = false;
	UIType getUIType() { return type; }
	UIType getUIContextType() { return context; }
	bool getAffectEntire() { return false; }
	bool inAutomationEditor() { return false; }
};
static session::State<RootUI> roots;
static RootUI automation;
static RootUI* getRootUI() {
	return &roots.active();
}
static RootUI& automation_view_for_session() {
	return automation;
}
static session::State<Clip*> clips;
static Clip* getCurrentClip() {
	return clips.active();
}
static bool isClipContext() {
	return true;
}
struct session_fixture {
	Clip* getClipForLayout() { return getCurrentClip(); }
};
static session_fixture session_view;
static session_fixture& session_view_for_session() {
	return session_view;
}
struct arranger_fixture {
	int yPressedEffective = 0;
	Output* outputsOnScreen[kDisplayHeight]{};
};
static session::State<arranger_fixture> arrangers;
static arranger_fixture& arranger_view_for_session() {
	return arrangers.active();
}
struct song_fixture {
	Clip* clip = nullptr;
	int lookups = 0;
	Clip* getClipWithOutput(Output*) {
		++lookups;
		return clip;
	}
};
static song_fixture song;
static song_fixture* currentSong = &song;
static struct {
	RecordingMode recording = RecordingMode::OFF;
} playbackHandler;
namespace indicator_leds {
static session::State<std::array<int, 3 + kNumModButtons>> states;
static const std::array<IndicatorLED, kNumModButtons> modLed = [] {
	std::array<IndicatorLED, kNumModButtons> result{};
	for (int i = 0; i < kNumModButtons; ++i)
		result[i] = static_cast<IndicatorLED>(3 + i);
	return result;
}();
static void setLedState(IndicatorLED led, bool on) {
	states.active()[static_cast<int>(led)] = on;
}
static void blinkLed(IndicatorLED led, int = 0, int = 0) {
	states.active()[static_cast<int>(led)] = 2;
}
} // namespace indicator_leds
struct View {
	bool displayVUMeter = false;
	int getModKnobMode() { return -1; }
	void setModLedStates();
};
static View view;
static View& view_for_session() {
	return view;
}
#include "mod_led.inc"
} // namespace mod_led_test
using namespace mod_led_test;
TEST_GROUP(ModLEDs){void setup() override{session::detail::active = session::Id::Local;
roots = {};
clips = {};
arrangers = {};
song = {};
currentSong = &song;
indicator_leds::states = {};
}
void teardown() override {
	session::detail::active = session::Id::Local;
}
}
;
TEST(ModLEDs, arranger_missing_clip_output_song_and_invalid_rows_are_safe) {
	Output output;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		roots.active().type = UIType::ARRANGER;
		auto& arranger = arrangers.active();
		arranger.outputsOnScreen[0] = &output;
		for (int row : {-1, 0, kDisplayHeight, kDisplayHeight + 1}) {
			arranger.yPressedEffective = row;
			view.setModLedStates();
			LONGS_EQUAL(1, indicator_leds::states.active()[1]);
		}
		arranger.yPressedEffective = 0;
		arranger.outputsOnScreen[0] = nullptr;
		view.setModLedStates();
		currentSong = nullptr;
		arranger.outputsOnScreen[0] = &output;
		view.setModLedStates();
		currentSong = &song;
	}
	LONGS_EQUAL(2, song.lookups);
}
TEST(ModLEDs, missing_or_audio_clip_does_not_supply_instrument_affect_state) {
	Clip audio;
	for (auto* clip : {static_cast<Clip*>(nullptr), &audio}) {
		clips.active() = clip;
		roots.active().context = UIType::INSTRUMENT_CLIP;
		view.setModLedStates();
		LONGS_EQUAL(0, indicator_leds::states.active()[0]);
	}
}
TEST(ModLEDs, keyboard_missing_clip_has_steady_clip_led) {
	roots.active().type = UIType::KEYBOARD_SCREEN;
	view.setModLedStates();
	LONGS_EQUAL(1, indicator_leds::states.active()[1]);
}
TEST(ModLEDs, shared_clip_uses_each_panels_automation_and_affect_state) {
	InstrumentClip clip;
	Output output;
	clip.automation.for_owner(session::Id::Remote) = true;
	clip.affect.for_owner(session::Id::Local) = true;
	song.clip = &clip;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		clips.active() = &clip;
		roots.active().context = UIType::INSTRUMENT_CLIP;
		for (auto type : {UIType::ARRANGER, UIType::KEYBOARD_SCREEN, UIType::SESSION}) {
			roots.active().type = type;
			arrangers.active().outputsOnScreen[0] = &output;
			view.setModLedStates();
			LONGS_EQUAL(owner == session::Id::Local ? 1 : 0, indicator_leds::states.active()[0]);
			LONGS_EQUAL(owner == session::Id::Remote ? 2 : 1, indicator_leds::states.active()[1]);
		}
	}
}
