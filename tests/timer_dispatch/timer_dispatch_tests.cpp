#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/ui/graphics_routing.h"
#include "gui/ui_timer_manager.h"
#include <functional>
namespace session = deluge::gui::ui_session;
enum class ActionResult { DEALT_WITH, REMIND_ME_OUTSIDE_CARD_ROUTINE };
enum class UIType { INSTRUMENT_CLIP };
enum class MIDIFollowFeedbackAutomationMode { DISABLED, LOW, MEDIUM, HIGH };
constexpr int kNoSelection = -1, kLowFeedbackAutomationRate = 1, kMediumFeedbackAutomationRate = 2,
              kHighFeedbackAutomationRate = 3;
constexpr int UART_ITEM_PIC_PADS = 0, kNumBytesInColUpdateMessage = 1;
int uartGetTxBufferSpace(int) {
	return 1000;
}
std::function<ActionResult()> on_timer;
int console_calls = 0, graphics_calls = 0, exit_calls = 0, hardware_calls = 0;
struct UI {
	virtual ~UI() = default;
	virtual ActionResult timerCallback() { return on_timer ? on_timer() : ActionResult::DEALT_WITH; }
	virtual ActionResult exitUI() {
		++exit_calls;
		return ActionResult::DEALT_WITH;
	}
	UIType getUIContextType() { return UIType::INSTRUMENT_CLIP; }
	void graphicsRoutine() { ++graphics_calls; }
	void flashDefaultRootNote() {}
	void midiLearnFlash() {}
	void flashPlayRoutine() {}
	void flushPendingModEncoderValuePopup() {}
	void blinkShortcut() {}
	void blinkInterpolationShortcut() {}
	void blinkPadSelectionShortcut() {}
	void blinkSelectedNoteRow() {}
	void gridPulseSelectedClip() {}
	bool inAutomationEditor() { return false; }
	void displayAutomation() {}
	UI* getCurrentMenuItem() { return this; }
	void readValueAgain() {}
	void sendMidiFollowFeedback(void*, int, bool) {}
};
UI view, keyboard, clip_view, automation, editor, song_view;
session::State<UI*> current_uis;
UI* getCurrentUI() {
	return current_uis.active();
}
UI* getRootUI() {
	return getCurrentUI();
}
UI& view_for_session() {
	return view;
}
UI& keyboard_screen_for_session() {
	return keyboard;
}
UI& instrument_clip_view_for_session() {
	return clip_view;
}
UI& automation_view_for_session() {
	return automation;
}
UI& sound_editor_for_session() {
	return editor;
}
UI& session_view_for_session() {
	return song_view;
}
namespace deluge::hid::display {
struct OLED {
	bool haveOLED() { return true; }
	void timerRoutine() {}
	void consoleTimerEvent() { ++console_calls; }
	static void scrollingAndBlinkingTimerEvent() {}
};
struct Screensaver {
	static void timerEvent() {}
};
inline bool have_oled_screen = true;
} // namespace deluge::hid::display
deluge::hid::display::OLED oled;
auto* display = &oled;
namespace deluge::hid::mirror {
inline bool client = false;
bool is_client() {
	return client;
}
} // namespace deluge::hid::mirror
namespace AudioEngine {
inline uint32_t audioSampleTimer = 1000;
}
struct Playback {
	void tapTempoAutoSwitchOff() {}
	bool isEitherClockActive() { return false; }
} playbackHandler;
struct {
	MIDIFollowFeedbackAutomationMode midiFollowFeedbackAutomation = MIDIFollowFeedbackAutomationMode::DISABLED;
} midiEngine;
struct {
	uint32_t timeAutomationFeedbackLastSent = 0;
} midiFollow;
namespace indicator_leds {
void ledBlinkTimeout(int) {
}
void blinkKnobIndicatorLevelTimeout() {
}
} // namespace indicator_leds
namespace PadLEDs {
void timerRoutine() {
}
} // namespace PadLEDs
namespace HIDSysex {
void sendDisplayIfChanged() {
}
} // namespace HIDSysex
void inputRoutine() {
	++hardware_calls;
}
void batteryLEDBlink() {
	++hardware_calls;
}
void oledLowLevelTimerCallback() {
	++hardware_calls;
}
UITimerManager::UITimerManager() = default;
#include "timer_dispatch.inc"
TEST_GROUP(TimerDispatch) {
	UITimerManager manager;
	UI ui;
	void setup() override {
		session::detail::active = session::Id::Local;
		current_uis.for_owner(session::Id::Local) = &ui;
		current_uis.for_owner(session::Id::Remote) = &ui;
		console_calls = graphics_calls = exit_calls = hardware_calls = 0;
		on_timer = {};
		deluge::hid::mirror::client = false;
		AudioEngine::audioSampleTimer = 1000;
	}
	void teardown() override {
		on_timer = {};
		session::detail::active = session::Id::Local;
	}
	void due(TimerName name) {
		manager.setTimerSamples(name, -1);
	}
};
TEST(TimerDispatch, missing_remote_ui_defers_timer_without_consuming_it) {
	session::Scope owner(session::Id::Remote);
	current_uis.active() = nullptr;
	due(TimerName::OLED_CONSOLE);
	manager.routine();
	LONGS_EQUAL(0, console_calls);
	CHECK(manager.isTimerSet(TimerName::OLED_CONSOLE));
	current_uis.active() = &ui;
	manager.routine();
	LONGS_EQUAL(1, console_calls);
	CHECK_FALSE(manager.isTimerSet(TimerName::OLED_CONSOLE));
}
TEST(TimerDispatch, callback_closing_remote_ui_stops_remaining_timer_dispatch) {
	session::Scope owner(session::Id::Remote);
	due(TimerName::UI_SPECIFIC);
	due(TimerName::OLED_CONSOLE);
	on_timer = [] {
		current_uis.active() = nullptr;
		return ActionResult::DEALT_WITH;
	};
	manager.routine();
	LONGS_EQUAL(0, console_calls);
	CHECK_FALSE(manager.isTimerSet(TimerName::UI_SPECIFIC));
	CHECK(manager.isTimerSet(TimerName::OLED_CONSOLE));
	CHECK(session::current() == session::Id::Remote);
}
TEST(TimerDispatch, local_hardware_timers_work_without_navigation) {
	current_uis.active() = nullptr;
	due(TimerName::READ_INPUTS);
	due(TimerName::BATT_LED_BLINK);
	manager.routine();
	LONGS_EQUAL(2, hardware_calls);
}
TEST(TimerDispatch, client_hardware_handshake_still_runs_with_no_remote_ui) {
	deluge::hid::mirror::client = true;
	session::Scope owner(session::Id::Remote);
	current_uis.active() = nullptr;
	due(TimerName::OLED_LOW_LEVEL);
	due(TimerName::OLED_CONSOLE);
	manager.routine();
	LONGS_EQUAL(1, hardware_calls);
	LONGS_EQUAL(0, console_calls);
	CHECK(manager.isTimerSet(TimerName::OLED_CONSOLE));
}
TEST(TimerDispatch, callback_closing_remote_ui_does_not_dereference_it_for_later_timers) {
	due(TimerName::READ_INPUTS);
	{
		session::Scope owner(session::Id::Remote);
		due(TimerName::UI_SPECIFIC);
		due(TimerName::BACK_MENU_EXIT);
		due(TimerName::GRAPHICS_ROUTINE);
		on_timer = [] {
			current_uis.active() = nullptr;
			return ActionResult::DEALT_WITH;
		};
		manager.routine();
		LONGS_EQUAL(0, exit_calls);
		LONGS_EQUAL(0, graphics_calls);
		LONGS_EQUAL(0, hardware_calls);
		CHECK(manager.isTimerSet(TimerName::BACK_MENU_EXIT));
		CHECK(manager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	}
	manager.routine();
	LONGS_EQUAL(1, hardware_calls);
}
TEST(TimerDispatch, replacement_remote_ui_can_receive_subsequent_timers) {
	session::Scope owner(session::Id::Remote);
	UI replacement_ui;
	due(TimerName::UI_SPECIFIC);
	due(TimerName::BACK_MENU_EXIT);
	due(TimerName::GRAPHICS_ROUTINE);
	on_timer = [&] {
		current_uis.active() = &replacement_ui;
		return ActionResult::DEALT_WITH;
	};
	manager.routine();
	LONGS_EQUAL(1, exit_calls);
	LONGS_EQUAL(1, graphics_calls);
	CHECK_FALSE(manager.isTimerSet(TimerName::BACK_MENU_EXIT));
	CHECK(manager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	current_uis.active() = &ui;
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
