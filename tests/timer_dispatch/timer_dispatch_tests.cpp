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
std::function<ActionResult()> on_timer, on_exit;
int console_calls = 0, graphics_calls = 0, exit_calls = 0, hardware_calls = 0;
int root_note_calls = 0;
struct UI {
	virtual ~UI() = default;
	virtual ActionResult timerCallback() { return on_timer ? on_timer() : ActionResult::DEALT_WITH; }
	virtual ActionResult exitUI() {
		++exit_calls;
		return on_exit ? on_exit() : ActionResult::DEALT_WITH;
	}
	UIType getUIContextType() { return UIType::INSTRUMENT_CLIP; }
	void graphicsRoutine() { ++graphics_calls; }
	void flashDefaultRootNote() { ++root_note_calls; }
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
		on_timer = on_exit = {};
		deluge::hid::mirror::client = false;
		AudioEngine::audioSampleTimer = 1000;
	}
	void teardown() override {
		on_timer = on_exit = {};
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
TEST(TimerDispatch, departed_ui_cannot_rearm_its_retry_on_either_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (auto name : {TimerName::UI_SPECIFIC, TimerName::BACK_MENU_EXIT}) {
			for (bool replace : {false, true}) {
				UITimerManager timers;
				UI replacement_ui;
				current_uis.active() = &ui;
				timers.setTimerSamples(name, -1);
				on_timer = on_exit = [&] {
					current_uis.active() = replace ? &replacement_ui : nullptr;
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				};
				timers.routine();
				CHECK_FALSE(timers.isTimerSet(name));
			}
		}
		current_uis.active() = &ui;
	}
}
TEST(TimerDispatch, retry_keeps_original_ui_timer_armed) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (auto name : {TimerName::UI_SPECIFIC, TimerName::BACK_MENU_EXIT}) {
			UITimerManager timers;
			timers.setTimerSamples(name, -1);
			on_timer = on_exit = [] { return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; };
			timers.routine();
			CHECK(timers.isTimerSet(name));
			LONGS_EQUAL(999, timers.getTimer(name).triggerTime);
		}
	}
}
TEST(TimerDispatch, replacement_ui_explicit_timer_keeps_its_new_deadline) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (auto name : {TimerName::UI_SPECIFIC, TimerName::BACK_MENU_EXIT}) {
			UITimerManager timers;
			UI replacement_ui;
			current_uis.active() = &ui;
			timers.setTimerSamples(name, -1);
			on_timer = on_exit = [&] {
				current_uis.active() = &replacement_ui;
				timers.setTimerSamples(name, 50);
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			};
			timers.routine();
			CHECK(timers.isTimerSet(name));
			LONGS_EQUAL(1050, timers.getTimer(name).triggerTime);
		}
		current_uis.active() = &ui;
	}
}
TEST(TimerDispatch, missing_local_ui_skips_navigation_callbacks_but_services_hardware) {
	current_uis.active() = nullptr;
	root_note_calls = 0;
	due(TimerName::DEFAULT_ROOT_NOTE);
	due(TimerName::UI_SPECIFIC);
	due(TimerName::BACK_MENU_EXIT);
	due(TimerName::GRAPHICS_ROUTINE);
	due(TimerName::READ_INPUTS);
	manager.routine();
	LONGS_EQUAL(0, exit_calls);
	LONGS_EQUAL(0, graphics_calls);
	LONGS_EQUAL(1, hardware_calls);
	LONGS_EQUAL(0, root_note_calls);
	CHECK_FALSE(manager.isTimerSet(TimerName::DEFAULT_ROOT_NOTE));
	CHECK_FALSE(manager.isTimerSet(TimerName::UI_SPECIFIC));
	CHECK_FALSE(manager.isTimerSet(TimerName::BACK_MENU_EXIT));
	CHECK(manager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	current_uis.active() = &ui;
	AudioEngine::audioSampleTimer += 1000;
	manager.routine();
	LONGS_EQUAL(1, graphics_calls);
}
TEST(TimerDispatch, local_callback_closing_navigation_preserves_hardware_service) {
	due(TimerName::UI_SPECIFIC);
	due(TimerName::BACK_MENU_EXIT);
	due(TimerName::GRAPHICS_ROUTINE);
	due(TimerName::READ_INPUTS);
	on_timer = [] {
		current_uis.active() = nullptr;
		return ActionResult::DEALT_WITH;
	};
	manager.routine();
	LONGS_EQUAL(0, exit_calls);
	LONGS_EQUAL(0, graphics_calls);
	LONGS_EQUAL(1, hardware_calls);
	CHECK_FALSE(manager.isTimerSet(TimerName::BACK_MENU_EXIT));
	CHECK(manager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
}
TEST(TimerDispatch, deadlines_across_clock_wrap_stay_with_their_panel) {
	AudioEngine::audioSampleTimer = UINT32_MAX - 10;
	manager.routine(); // Service the empty bank at the simulated pre-wrap time.
	manager.setTimerSamples(TimerName::OLED_CONSOLE, 5);
	{
		session::Scope owner(session::Id::Remote);
		manager.routine(); // Service the empty bank at the simulated pre-wrap time.
		manager.setTimerSamples(TimerName::OLED_CONSOLE, 20);
	}
	AudioEngine::audioSampleTimer = UINT32_MAX - 5;
	manager.routine();
	LONGS_EQUAL(0, console_calls);
	++AudioEngine::audioSampleTimer;
	manager.routine();
	LONGS_EQUAL(1, console_calls);
	{
		session::Scope owner(session::Id::Remote);
		manager.routine();
		LONGS_EQUAL(1, console_calls);
		CHECK(manager.isTimerSet(TimerName::OLED_CONSOLE));
		AudioEngine::audioSampleTimer = 9;
		manager.routine();
		LONGS_EQUAL(1, console_calls);
		AudioEngine::audioSampleTimer = 10;
		manager.routine();
		LONGS_EQUAL(2, console_calls);
		CHECK_FALSE(manager.isTimerSet(TimerName::OLED_CONSOLE));
	}
	manager.routine();
	LONGS_EQUAL(2, console_calls);
}
TEST(TimerDispatch, deferred_remote_timer_survives_clock_wrap_without_rearming_local_timer) {
	AudioEngine::audioSampleTimer = UINT32_MAX - 10;
	manager.routine(); // Service the empty bank at the simulated pre-wrap time.
	manager.setTimerSamples(TimerName::OLED_CONSOLE, 30);
	{
		session::Scope owner(session::Id::Remote);
		manager.routine(); // Service the empty bank at the simulated pre-wrap time.
		manager.setTimerSamples(TimerName::OLED_CONSOLE, 5);
		current_uis.active() = nullptr;
		AudioEngine::audioSampleTimer = UINT32_MAX - 4;
		manager.routine();
		CHECK(manager.isTimerSet(TimerName::OLED_CONSOLE));
		LONGS_EQUAL(UINT32_MAX - 5, manager.getTimer(TimerName::OLED_CONSOLE).triggerTime);
		AudioEngine::audioSampleTimer = 0;
		current_uis.active() = &ui;
		manager.routine();
		LONGS_EQUAL(1, console_calls);
		CHECK_FALSE(manager.isTimerSet(TimerName::OLED_CONSOLE));
	}
	manager.routine();
	LONGS_EQUAL(1, console_calls);
	LONGS_EQUAL(19, manager.getTimer(TimerName::OLED_CONSOLE).triggerTime);
	AudioEngine::audioSampleTimer = 20;
	manager.routine();
	LONGS_EQUAL(2, console_calls);
}
TEST(TimerDispatch, first_timer_in_idle_bank_is_serviced_after_wrap) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		UITimerManager timers;
		AudioEngine::audioSampleTimer = UINT32_MAX - 10;
		timers.setTimerSamples(TimerName::OLED_CONSOLE, 30);
		AudioEngine::audioSampleTimer = 20;
		int previous_calls = console_calls;
		timers.routine();
		LONGS_EQUAL(previous_calls + 1, console_calls);
		CHECK_FALSE(timers.isTimerSet(TimerName::OLED_CONSOLE));
	}
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
