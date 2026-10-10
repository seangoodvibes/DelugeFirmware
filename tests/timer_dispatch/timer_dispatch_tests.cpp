#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/ui/graphics_routing.h"
#include "gui/ui_timer_manager.h"
#include "util/lifetime.h"
#include <functional>
#include <new>
namespace session = deluge::gui::ui_session;
struct Song {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
Song original_song, replacement_song;
Song* currentSong = &original_song;
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
std::function<void()> on_graphics, on_input, on_automation, on_levels, on_menu_read;
int menu_reads = 0, automation_calls = 0;
int console_calls = 0, graphics_calls = 0, exit_calls = 0, hardware_calls = 0;
int root_note_calls = 0;
struct UI {
	bool automation_editor = false;
	UI* menu = this;
	virtual ~UI() = default;
	virtual ActionResult timerCallback() { return on_timer ? on_timer() : ActionResult::DEALT_WITH; }
	virtual ActionResult exitUI() {
		++exit_calls;
		return on_exit ? on_exit() : ActionResult::DEALT_WITH;
	}
	UIType getUIContextType() { return UIType::INSTRUMENT_CLIP; }
	void graphicsRoutine() {
		++graphics_calls;
		if (on_graphics)
			on_graphics();
	}
	void flashDefaultRootNote() { ++root_note_calls; }
	void midiLearnFlash() {}
	void flashPlayRoutine() {}
	void flushPendingModEncoderValuePopup() {}
	void blinkShortcut() {}
	void blinkInterpolationShortcut() {}
	void blinkPadSelectionShortcut() {}
	void blinkSelectedNoteRow() {}
	void gridPulseSelectedClip() {}
	bool inAutomationEditor() { return automation_editor; }
	void displayAutomation() {
		++automation_calls;
		if (on_automation)
			on_automation();
	}
	UI* getCurrentMenuItem() { return menu; }
	void readValueAgain() {
		++menu_reads;
		if (on_menu_read)
			on_menu_read();
	}
	void sendMidiFollowFeedback(void*, int, bool) {}
};
struct View : UI {
	bool pendingParamAutomationUpdatesModLevels = false;
	void setKnobIndicatorLevels() {
		if (on_levels)
			on_levels();
	}
	void displayAutomation();
};
View view;
UI keyboard, clip_view, automation, editor, song_view;
session::State<UI*> current_uis, root_uis;
UI* getCurrentUI() {
	return current_uis.active();
}
UI* getRootUI() {
	return root_uis.active() ? root_uis.active() : getCurrentUI();
}
View& view_for_session() {
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
	if (on_input)
		on_input();
}
void batteryLEDBlink() {
	++hardware_calls;
}
void oledLowLevelTimerCallback() {
	++hardware_calls;
}
UITimerManager::UITimerManager() = default;
#include "timer_dispatch.inc"
#include "view_automation.inc"
TEST_GROUP(TimerDispatch) {
	UITimerManager manager;
	UI ui;
	void setup() override {
		session::detail::active = session::Id::Local;
		current_uis.for_owner(session::Id::Local) = &ui;
		current_uis.for_owner(session::Id::Remote) = &ui;
		console_calls = graphics_calls = exit_calls = hardware_calls = 0;
		on_timer = on_exit = {};
		on_graphics = on_input = on_automation = on_levels = on_menu_read = {};
		view.pendingParamAutomationUpdatesModLevels = false;
		root_uis = {};
		menu_reads = automation_calls = 0;
		automation.automation_editor = false;
		editor.menu = &editor;
		deluge::hid::mirror::client = false;
		AudioEngine::audioSampleTimer = 1000;
		currentSong = &original_song;
	}
	void teardown() override {
		on_timer = on_exit = {};
		on_graphics = on_input = on_automation = on_levels = on_menu_read = {};
		view.pendingParamAutomationUpdatesModLevels = false;
		root_uis = {};
		menu_reads = automation_calls = 0;
		automation.automation_editor = false;
		editor.menu = &editor;
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
TEST(TimerDispatch, callback_owner_change_stops_later_timers_and_restores_caller) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		UITimerManager timers;
		timers.setTimerSamples(TimerName::UI_SPECIFIC, -1);
		timers.setTimerSamples(TimerName::OLED_CONSOLE, -1);
		on_timer = [=] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
			return ActionResult::DEALT_WITH;
		};
		int previous_calls = console_calls;
		timers.routine();
		CHECK(session::current() == owner);
		LONGS_EQUAL(previous_calls, console_calls);
		CHECK(timers.isTimerSet(TimerName::OLED_CONSOLE));
		on_timer = {};
		timers.routine();
		LONGS_EQUAL(previous_calls + 1, console_calls);
	}
}
TEST(TimerDispatch, retry_cannot_cross_owner_even_with_identical_ui_pointer) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (auto name : {TimerName::UI_SPECIFIC, TimerName::BACK_MENU_EXIT}) {
			UITimerManager timers;
			timers.setTimerSamples(name, -1);
			on_timer = on_exit = [=] {
				session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			};
			timers.routine();
			CHECK(session::current() == owner);
			CHECK_FALSE(timers.isTimerSet(name));
		}
	}
}
TEST(TimerDispatch, graphics_owner_change_does_not_schedule_peer_timer) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		UITimerManager timers;
		timers.setTimerSamples(TimerName::GRAPHICS_ROUTINE, -1);
		const auto peer = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		{
			session::Scope peer_scope(peer);
			timers.setTimerSamples(TimerName::GRAPHICS_ROUTINE, 70);
		}
		on_graphics = [=] { session::detail::active = peer; };
		timers.routine();
		CHECK(session::current() == owner);
		session::Scope peer_scope(peer);
		LONGS_EQUAL(1070, timers.getTimer(TimerName::GRAPHICS_ROUTINE).triggerTime);
	}
}
TEST(TimerDispatch, client_takeover_during_input_defers_remaining_timers) {
	due(TimerName::READ_INPUTS);
	due(TimerName::BATT_LED_BLINK);
	due(TimerName::GRAPHICS_ROUTINE);
	due(TimerName::OLED_LOW_LEVEL);
	due(TimerName::OLED_CONSOLE);
	on_input = [] { deluge::hid::mirror::client = true; };
	manager.routine();
	LONGS_EQUAL(1, hardware_calls);
	LONGS_EQUAL(0, graphics_calls);
	LONGS_EQUAL(0, console_calls);
	CHECK(manager.isTimerSet(TimerName::BATT_LED_BLINK));
	CHECK(manager.isTimerSet(TimerName::GRAPHICS_ROUTINE));
	CHECK(manager.isTimerSet(TimerName::OLED_LOW_LEVEL));
	CHECK(manager.isTimerSet(TimerName::OLED_CONSOLE));
	LONGS_EQUAL(999, manager.getTimer(TimerName::GRAPHICS_ROUTINE).triggerTime);
	manager.routine();
	LONGS_EQUAL(2, hardware_calls);
	LONGS_EQUAL(0, graphics_calls);
	LONGS_EQUAL(0, console_calls);
	CHECK_FALSE(manager.isTimerSet(TimerName::OLED_LOW_LEVEL));
	deluge::hid::mirror::client = false;
	manager.routine();
	LONGS_EQUAL(3, hardware_calls);
	LONGS_EQUAL(1, graphics_calls);
	LONGS_EQUAL(1, console_calls);
}

TEST(TimerDispatch, automation_refresh_does_not_read_menu_after_context_changes) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int scenario = 0; scenario < 4; ++scenario) {
			UITimerManager timers;
			UI replacement;
			current_uis.active() = &editor;
			root_uis.active() = &automation;
			automation.automation_editor = true;
			editor.menu = &editor;
			deluge::hid::mirror::client = false;
			on_automation = [&] {
				if (scenario == 0)
					session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
				if (scenario == 1)
					root_uis.active() = &replacement;
				if (scenario == 2)
					editor.menu = &replacement;
				if (scenario == 3)
					deluge::hid::mirror::client = true;
			};
			timers.setTimerSamples(TimerName::DISPLAY_AUTOMATION, -1);
			timers.routine();
			CHECK(session::current() == owner);
			LONGS_EQUAL(0, menu_reads);
		}
	}
}
TEST(TimerDispatch, automation_refresh_reads_unchanged_menu_and_skips_missing_menu) {
	current_uis.active() = &editor;
	root_uis.active() = &automation;
	automation.automation_editor = true;
	due(TimerName::DISPLAY_AUTOMATION);
	manager.routine();
	LONGS_EQUAL(1, menu_reads);
	editor.menu = nullptr;
	due(TimerName::DISPLAY_AUTOMATION);
	manager.routine();
	LONGS_EQUAL(1, menu_reads);
}

TEST(TimerDispatch, fallback_automation_stops_after_indicator_context_change) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int scenario = 0; scenario < 5; ++scenario) {
			UITimerManager timers;
			UI replacement;
			current_uis.active() = &editor;
			root_uis.active() = &ui;
			editor.menu = &editor;
			view.pendingParamAutomationUpdatesModLevels = true;
			deluge::hid::mirror::client = false;
			on_levels = [&] {
				if (scenario == 0)
					session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
				if (scenario == 1)
					root_uis.active() = &replacement;
				if (scenario == 2)
					editor.menu = &replacement;
				if (scenario == 3)
					current_uis.active() = &replacement;
				if (scenario == 4)
					deluge::hid::mirror::client = true;
			};
			timers.setTimerSamples(TimerName::DISPLAY_AUTOMATION, -1);
			timers.routine();
			CHECK(session::current() == owner);
			LONGS_EQUAL(0, menu_reads);
		}
	}
}
TEST(TimerDispatch, fallback_automation_reads_valid_menu_and_skips_missing_menu) {
	current_uis.active() = &editor;
	root_uis.active() = &ui;
	due(TimerName::DISPLAY_AUTOMATION);
	manager.routine();
	LONGS_EQUAL(1, menu_reads);
	editor.menu = nullptr;
	due(TimerName::DISPLAY_AUTOMATION);
	manager.routine();
	LONGS_EQUAL(1, menu_reads);
}
TEST(TimerDispatch, automation_menu_read_does_not_follow_song_replacement) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (bool automation_root : {false, true}) {
			UITimerManager timers;
			currentSong = &original_song;
			current_uis.active() = &editor;
			root_uis.active() = automation_root ? &automation : &ui;
			automation.automation_editor = true;
			view.pendingParamAutomationUpdatesModLevels = true;
			on_automation = on_levels = [] { currentSong = &replacement_song; };
			timers.setTimerSamples(TimerName::DISPLAY_AUTOMATION, -1);
			timers.routine();
			POINTERS_EQUAL(&replacement_song, currentSong);
			LONGS_EQUAL(0, menu_reads);
		}
	}
}
TEST(TimerDispatch, fallback_direct_call_restores_owner_after_indicator_callback) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		current_uis.active() = &editor;
		root_uis.active() = &ui;
		view.pendingParamAutomationUpdatesModLevels = true;
		on_levels = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		view.displayAutomation();
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, menu_reads);
	}
}
TEST(TimerDispatch, fallback_menu_removed_during_indicator_update_is_not_read) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		current_uis.active() = &editor;
		root_uis.active() = &ui;
		editor.menu = &editor;
		view.pendingParamAutomationUpdatesModLevels = true;
		on_levels = [] { editor.menu = nullptr; };
		view.displayAutomation();
		LONGS_EQUAL(0, menu_reads);
	}
}
TEST(TimerDispatch, automation_menu_callback_owner_change_defers_later_timers) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		UITimerManager timers;
		current_uis.active() = &editor;
		root_uis.active() = &automation;
		automation.automation_editor = true;
		on_menu_read = [owner] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		timers.setTimerSamples(TimerName::DISPLAY_AUTOMATION, -1);
		timers.setTimerSamples(TimerName::OLED_CONSOLE, -1);
		timers.routine();
		CHECK(session::current() == owner);
		CHECK(timers.isTimerSet(TimerName::OLED_CONSOLE));
		LONGS_EQUAL(0, console_calls);
	}
	LONGS_EQUAL(2, menu_reads);
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}

TEST(TimerDispatch, automation_fallback_song_reuse_during_indicators_skips_menu_read) {
	current_uis.active() = &editor;
	root_uis.active() = &ui;
	view.pendingParamAutomationUpdatesModLevels = true;
	on_levels = [] {
		original_song.~Song();
		new (&original_song) Song;
	};
	view.displayAutomation();
	LONGS_EQUAL(0, menu_reads);
}
TEST(TimerDispatch, automation_fallback_retired_song_does_not_refresh) {
	Song retiring_song;
	retiring_song.lifetime.retire();
	currentSong = &retiring_song;
	current_uis.active() = &editor;
	view.pendingParamAutomationUpdatesModLevels = true;
	int level_calls = 0;
	on_levels = [&] { ++level_calls; };
	view.displayAutomation();
	LONGS_EQUAL(0, level_calls);
	LONGS_EQUAL(0, menu_reads);
	currentSong = &original_song;
}

TEST(TimerDispatch, automation_timer_song_reuse_during_render_skips_menu_read) {
	current_uis.active() = &editor;
	root_uis.active() = &automation;
	automation.automation_editor = true;
	on_automation = [] {
		original_song.~Song();
		new (&original_song) Song;
	};
	UITimerManager timers;
	timers.setTimerSamples(TimerName::DISPLAY_AUTOMATION, -1);
	timers.routine();
	LONGS_EQUAL(1, automation_calls);
	LONGS_EQUAL(0, menu_reads);
}

TEST(TimerDispatch, song_invalidation_cancels_retry_and_defers_remaining_batch) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (auto name : {TimerName::UI_SPECIFIC, TimerName::BACK_MENU_EXIT}) {
			for (int invalidation = 0; invalidation < 3; ++invalidation) {
				Song source_song;
				currentSong = &source_song;
				UITimerManager timers;
				timers.setTimerSamples(name, -1);
				timers.setTimerSamples(TimerName::OLED_CONSOLE, -1);
				on_timer = on_exit = [&] {
					if (invalidation == 0)
						currentSong = &replacement_song;
					else if (invalidation == 1)
						source_song.lifetime.retire();
					else {
						source_song.~Song();
						new (&source_song) Song;
					}
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				};
				const int previous_calls = console_calls;
				timers.routine();
				CHECK_FALSE(timers.isTimerSet(name));
				LONGS_EQUAL(previous_calls, console_calls);
				CHECK(timers.isTimerSet(TimerName::OLED_CONSOLE));
				LONGS_EQUAL(999, timers.getTimer(TimerName::OLED_CONSOLE).triggerTime);
				on_timer = on_exit = {};
				currentSong = &original_song;
				timers.routine();
				LONGS_EQUAL(previous_calls + 1, console_calls);
			}
		}
	}
}

TEST(TimerDispatch, retired_song_defers_entire_timer_batch) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		Song source_song;
		source_song.lifetime.retire();
		currentSong = &source_song;
		UITimerManager timers;
		timers.setTimerSamples(TimerName::BACK_MENU_EXIT, -1);
		timers.routine();
		LONGS_EQUAL(0, exit_calls);
		CHECK(timers.isTimerSet(TimerName::BACK_MENU_EXIT));
		currentSong = &original_song;
	}
}

TEST(TimerDispatch, no_song_context_preserves_timer_retry_and_hardware_service) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		currentSong = nullptr;
		UITimerManager timers;
		timers.setTimerSamples(TimerName::UI_SPECIFIC, -1);
		timers.setTimerSamples(TimerName::OLED_CONSOLE, -1);
		on_timer = [] { return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; };
		const int previous_calls = console_calls;
		timers.routine();
		CHECK(timers.isTimerSet(TimerName::UI_SPECIFIC));
		LONGS_EQUAL(previous_calls + 1, console_calls);
	}
	currentSong = &original_song;
}

TEST(TimerDispatch, graphics_invalidated_context_does_not_rearm_timer) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (bool takeover : {false, true}) {
			UITimerManager timers;
			currentSong = &original_song;
			deluge::hid::mirror::client = false;
			timers.setTimerSamples(TimerName::GRAPHICS_ROUTINE, -1);
			on_graphics = [=] {
				if (takeover)
					deluge::hid::mirror::client = true;
				else {
					original_song.~Song();
					new (&original_song) Song;
				}
			};
			timers.routine();
			CHECK_FALSE(timers.isTimerSet(TimerName::GRAPHICS_ROUTINE));
		}
	}
	deluge::hid::mirror::client = false;
}
