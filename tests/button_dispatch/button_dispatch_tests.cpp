#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <functional>
#include <new>
namespace session = deluge::gui::ui_session;
enum class ActionResult { DEALT_WITH, NOT_DEALT_WITH, REMIND_ME_OUTSIDE_CARD_ROUTINE };
enum class RuntimeFeatureSettingType { EmulatedDisplay };
enum class RuntimeFeatureStateEmulatedDisplay { Hardware };
enum class AudioInputChannel { NONE, OUTPUT };
enum class PopupType { THRESHOLD_RECORDING_MODE };
enum class TimerName { BACK_MENU_EXIT, OLED_LOW_LEVEL };
constexpr int kNumModButtons = 1, kInternalButtonPressLatency = 0, kShortPressTime = 100, LONG_PRESS_DURATION = 100;
constexpr int modButtonX[] = {30}, modButtonY[] = {0};
namespace deluge::hid {
using Button = int;
namespace button {
enum {
	AFFECT_ENTIRE,
	BACK,
	SELECT_ENC,
	PLAY,
	SHIFT,
	RECORD,
	TEMPO_ENC,
	TAP_TEMPO,
	LEARN,
	MOD_ENCODER_0,
	MOD_ENCODER_1,
	CROSS_SCREEN_EDIT
};
struct Coordinates {
	int x, y;
};
Coordinates toXY(Button b) {
	return {b, 0};
}
} // namespace button
namespace mirror {
bool client = false;
bool is_client() {
	return client;
}
} // namespace mirror
namespace display {
void swapDisplayType() {
}
} // namespace display
} // namespace deluge::hid
struct Song {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
};
Song song;
Song* currentSong = &song;
std::function<ActionResult()> on_button, on_pad;
int pad_calls = 0;
struct {
	bool processStarted = false;
} stemExport;
constexpr int kDisplayWidth = 16, kSideBarWidth = 2, kDisplayHeight = 8;
std::function<void()> on_play, on_record, on_popup;
int button_calls = 0, play_calls = 0, record_calls = 0, mod_calls = 0;
struct UI {
	ActionResult padAction(int32_t, int32_t, int32_t) {
		++pad_calls;
		return on_pad ? on_pad() : ActionResult::DEALT_WITH;
	}
	ActionResult buttonAction(deluge::hid::Button, bool, bool) {
		++button_calls;
		return on_button ? on_button() : ActionResult::NOT_DEALT_WITH;
	}
	void modButtonAction(int, bool) { ++mod_calls; }
	void modEncoderButtonAction(int, bool) { ++mod_calls; }
	bool isLoadingSong() { return false; }
} ui, replacement_ui, session_ui, arranger_ui, load_ui;
session::State<UI*> current_uis;
UI* getCurrentUI() {
	return current_uis.active();
}
UI& session_view_for_session() {
	return session_ui;
}
UI& arranger_view_for_session() {
	return arranger_ui;
}
UI& load_song_ui_for_session() {
	return load_ui;
}
struct Display {
	void cancelPopup() {
		if (on_popup)
			on_popup();
	}
	bool haveOLED() { return true; }
	bool hasPopupOfType(PopupType) { return false; }
} display_instance;
Display* display = &display_instance;
struct {
	RuntimeFeatureStateEmulatedDisplay get(RuntimeFeatureSettingType) {
		return RuntimeFeatureStateEmulatedDisplay::Hardware;
	}
} runtimeFeatureSettings;
struct {
	void setTimer(TimerName, int) {}
	void unsetTimer(TimerName) {}
} uiTimerManager;
namespace AudioEngine {
uint32_t audioSampleTimer = 1000;
}
struct Playback {
	bool playbackState = true;
	bool isEitherClockActive() { return true; }
	void playButtonPressed(int) {
		++play_calls;
		if (on_play)
			on_play();
	}
	void recordButtonPressed() { ++record_calls; }
	void stopOutputRecordingAtLoopEnd() {
		if (on_play)
			on_play();
	}
	void commandClearTempoAutomation() {}
	void commandDisplaySwingInterval() {}
	void commandDisplayTempo() {}
} playbackHandler;
auto* currentPlaybackMode = &playbackHandler;
struct Recorder {
	AudioInputChannel recordingSource = AudioInputChannel::NONE;
	void beginOutputRecording() {
		++record_calls;
		if (on_record)
			on_record();
	}
	bool isCurrentlyResampling() { return false; }
	void endRecordingSoon(int) {}
};
session::State<Recorder> recorders;
Recorder& audio_recorder_for_session() {
	return recorders.active();
}
namespace Buttons {
struct State {
	bool buttonStates[32][1]{};
	bool considerShiftReleaseForSticky = false, considerCrossScreenReleaseForCrossScreenMode = false;
	bool selectButtonPressUsedUp = false, recordButtonPressUsedUp = false;
	uint32_t timeRecordButtonPressed = 0;
};
session::State<State> states;
State& state() {
	return states.active();
}
bool isButtonPressed(deluge::hid::Button b) {
	return state().buttonStates[b][0];
}
bool isShiftButtonPressed() {
	return isButtonPressed(deluge::hid::button::SHIFT);
}
void commandToggleShift(bool) {
}
#include "button_dispatch.inc"
} // namespace Buttons
class MatrixDriver {
public:
	struct State {
		bool padStates[kDisplayWidth + kSideBarWidth][kDisplayHeight]{};
	};
	session::State<State> states_;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);
	bool isPadPressed(int32_t x, int32_t y);
	void noPressesHappening(bool);
};
#include "pad_dispatch.inc"
namespace hid = deluge::hid;
namespace util {
template <typename T>
auto to_underlying(T value) {
	return static_cast<int>(value);
}
} // namespace util
namespace PIC {
enum class Response : uint8_t { NEXT_PAD_OFF = 33, NO_PRESSES_HAPPENING = 34 };
constexpr auto kPadAndButtonMessagesEnd = static_cast<Response>(32);
} // namespace PIC
struct Pad {
	int x = 0, y = 0;
	explicit Pad(int) {}
	static bool isPad(int value) { return value == 20; }
};
constexpr int USE_DEFAULT_VELOCITY = 255, UART_ITEM_PIC = 0;
#define D_PRINTLN(...)                                                                                                 \
	do {                                                                                                               \
	} while (0)
int nextPadPressIsOn = USE_DEFAULT_VELOCITY;
bool waitingForSDRoutineToEnd = false, usbInitializationPeriodComplete = true, sdRoutineLock = false;
uint32_t timeUSBInitializationEnds = 0;
int oledWaitingForMessage = 35;
bool pic_available = false;
PIC::Response pic_response{};
int pic_replays = 0, sticky_changes = 0, shift_led_updates = 0, button_release_calls = 0, pad_release_calls = 0;
std::function<void()> on_local_input, on_pad_release;
bool local_input_consumed = false;
bool uartGetChar(int, char* value) {
	if (!pic_available)
		return false;
	pic_available = false;
	*value = static_cast<char>(pic_response);
	return true;
}
void uartPutCharBack(int) {
	++pic_replays;
}
namespace deluge::hid::display {
bool have_oled_screen = true;
namespace Screensaver {
void noteActivity() {
}
} // namespace Screensaver
} // namespace deluge::hid::display
namespace deluge::hid::mirror {
bool local_input(int, bool) {
	if (on_local_input)
		on_local_input();
	return local_input_consumed;
}
bool local_all_released() {
	return false;
}
} // namespace deluge::hid::mirror
namespace Buttons {
void ignoreCurrentShiftForSticky() {
	++sticky_changes;
	state().considerShiftReleaseForSticky = false;
}
void update_shift_led() {
	++shift_led_updates;
}
void noPressesHappening(bool) {
	++button_release_calls;
}
} // namespace Buttons
MatrixDriver matrixDriver;
void MatrixDriver::noPressesHappening(bool) {
	++pad_release_calls;
	if (on_pad_release)
		on_pad_release();
}
#include "physical_dispatch.inc"
#undef D_PRINTLN

// clang-format off
TEST_GROUP(ButtonDispatch) {
	void setup() override {
		session::detail::active = session::Id::Local;
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			current_uis.for_owner(owner) = &ui;
			Buttons::states.for_owner(owner) = {};
			recorders.for_owner(owner) = {};
		}
		on_button = on_pad = {};
		on_play = on_record = on_popup = {};
		button_calls = play_calls = record_calls = mod_calls = pad_calls = 0;
		stemExport.processStarted = false;
		pic_available = false;
		sdRoutineLock = waitingForSDRoutineToEnd = false;
		nextPadPressIsOn = USE_DEFAULT_VELOCITY;
		usbInitializationPeriodComplete = true;
		on_local_input = on_pad_release = {};
		local_input_consumed = false;
		pic_replays = sticky_changes = shift_led_updates = button_release_calls = pad_release_calls = 0;
		matrixDriver.states_ = {};
		currentSong = &song;
		deluge::hid::mirror::client = false;
	}
	void teardown() override {
		on_local_input = on_pad_release = {};
		on_button = on_pad = {};
		on_play = on_record = on_popup = {};
		currentSong = &song;
		session::detail::active = session::Id::Local;
	}
};
// clang-format on

TEST(ButtonDispatch, invalidated_ui_callback_cannot_start_playback_or_retry) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (auto result : {ActionResult::NOT_DEALT_WITH, ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE}) {
			for (int invalidation = 0; invalidation < 4; ++invalidation) {
				current_uis.active() = &ui;
				deluge::hid::mirror::client = false;
				on_button = [=] {
					if (invalidation == 0) {
						song.~Song();
						new (&song) Song;
					}
					else if (invalidation == 1)
						current_uis.active() = &replacement_ui;
					else if (invalidation == 2)
						deluge::hid::mirror::client = true;
					else
						session::detail::active =
						    owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
					return result;
				};
				CHECK(Buttons::buttonAction(deluge::hid::button::PLAY, true, false) == ActionResult::DEALT_WITH);
				LONGS_EQUAL(0, play_calls);
				CHECK(session::current() == owner);
			}
		}
	}
}

TEST(ButtonDispatch, play_callback_song_reuse_does_not_start_recording_or_consume_record_hold) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		Buttons::state().buttonStates[deluge::hid::button::RECORD][0] = true;
		on_play = [] {
			song.~Song();
			new (&song) Song;
		};
		Buttons::buttonAction(deluge::hid::button::PLAY, true, false);
		LONGS_EQUAL(0, record_calls);
		CHECK_FALSE(Buttons::state().recordButtonPressUsedUp);
	}
}

TEST(ButtonDispatch, recording_callback_cannot_mark_another_panels_record_hold_used) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		Buttons::states = {};
		Buttons::state().buttonStates[deluge::hid::button::SHIFT][0] = true;
		on_record = [=] {
			session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		};
		Buttons::buttonAction(deluge::hid::button::RECORD, true, false);
		CHECK(session::current() == owner);
		CHECK_FALSE(Buttons::states.for_owner(session::Id::Local).recordButtonPressUsedUp);
		CHECK_FALSE(Buttons::states.for_owner(session::Id::Remote).recordButtonPressUsedUp);
	}
}

TEST(ButtonDispatch, popup_callback_replacing_ui_stops_button_dispatch) {
	on_popup = [] { current_uis.active() = &replacement_ui; };
	Buttons::buttonAction(deluge::hid::button::AFFECT_ENTIRE, true, false);
	LONGS_EQUAL(0, button_calls);
}

TEST(ButtonDispatch, normal_play_and_card_retry_keep_existing_behavior) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		on_button = {};
		int previous_calls = play_calls;
		Buttons::buttonAction(deluge::hid::button::PLAY, true, false);
		LONGS_EQUAL(previous_calls + 1, play_calls);
		on_button = [] { return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; };
		CHECK(Buttons::buttonAction(deluge::hid::button::PLAY, true, true)
		      == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE);
		LONGS_EQUAL(previous_calls + 1, play_calls);
	}
}
TEST(ButtonDispatch, invalid_entry_still_records_release_without_dispatching) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int invalidation = 0; invalidation < 3; ++invalidation) {
			Song retired_song;
			retired_song.lifetime.retire();
			currentSong = invalidation == 0 ? &retired_song : &song;
			current_uis.active() = invalidation == 1 ? nullptr : &ui;
			deluge::hid::mirror::client = invalidation == 2;
			Buttons::state().buttonStates[deluge::hid::button::RECORD][0] = true;
			CHECK(Buttons::buttonAction(deluge::hid::button::RECORD, false, false) == ActionResult::DEALT_WITH);
			CHECK_FALSE(Buttons::isButtonPressed(deluge::hid::button::RECORD));
			LONGS_EQUAL(0, button_calls);
			LONGS_EQUAL(0, record_calls);
		}
	}
}

TEST(ButtonDispatch, no_song_menu_still_receives_button) {
	currentSong = nullptr;
	on_button = [] { return ActionResult::DEALT_WITH; };
	Buttons::buttonAction(deluge::hid::button::SELECT_ENC, true, false);
	LONGS_EQUAL(1, button_calls);
}

TEST(ButtonDispatch, unchanged_play_context_starts_recording_and_consumes_hold) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		Buttons::state().buttonStates[deluge::hid::button::RECORD][0] = true;
		const int previous_calls = record_calls;
		Buttons::buttonAction(deluge::hid::button::PLAY, true, false);
		LONGS_EQUAL(previous_calls + 1, record_calls);
		CHECK(Buttons::state().recordButtonPressUsedUp);
	}
}

TEST(ButtonDispatch, play_recording_callback_song_reuse_does_not_consume_hold) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		Buttons::state().buttonStates[deluge::hid::button::RECORD][0] = true;
		on_record = [] {
			song.~Song();
			new (&song) Song;
		};
		Buttons::buttonAction(deluge::hid::button::PLAY, true, false);
		CHECK_FALSE(Buttons::state().recordButtonPressUsedUp);
	}
}

int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}

TEST(ButtonDispatch, pad_retry_is_cancelled_when_callback_invalidates_context) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int invalidation = 0; invalidation < 4; ++invalidation) {
			MatrixDriver matrix;
			current_uis.active() = &ui;
			deluge::hid::mirror::client = false;
			on_pad = [=] {
				if (invalidation == 0) {
					song.~Song();
					new (&song) Song;
				}
				else if (invalidation == 1)
					current_uis.active() = &replacement_ui;
				else if (invalidation == 2)
					deluge::hid::mirror::client = true;
				else
					session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			};
			CHECK(matrix.padAction(0, 0, 100) == ActionResult::DEALT_WITH);
			CHECK(session::current() == owner);
			CHECK(matrix.isPadPressed(0, 0));
			const auto peer = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
			CHECK_FALSE(matrix.states_.for_owner(peer).padStates[0][0]);
		}
	}
}

TEST(ButtonDispatch, invalid_pad_context_records_release_without_invoking_ui) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int invalidation = 0; invalidation < 3; ++invalidation) {
			MatrixDriver matrix;
			matrix.states_.active().padStates[0][0] = true;
			Song retired_song;
			retired_song.lifetime.retire();
			currentSong = invalidation == 0 ? &retired_song : &song;
			current_uis.active() = invalidation == 1 ? nullptr : &ui;
			deluge::hid::mirror::client = invalidation == 2;
			CHECK(matrix.padAction(0, 0, 0) == ActionResult::DEALT_WITH);
			CHECK_FALSE(matrix.isPadPressed(0, 0));
			LONGS_EQUAL(0, pad_calls);
		}
	}
}

TEST(ButtonDispatch, valid_pad_retry_and_no_song_context_are_preserved) {
	MatrixDriver matrix;
	currentSong = nullptr;
	on_pad = [] { return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; };
	CHECK(matrix.padAction(0, 0, 100) == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE);
	CHECK(matrix.isPadPressed(0, 0));
	on_pad = {};
	CHECK(matrix.padAction(0, 0, 0) == ActionResult::DEALT_WITH);
	CHECK_FALSE(matrix.isPadPressed(0, 0));
}

TEST(ButtonDispatch, invalid_pad_coordinates_and_stem_export_do_not_dispatch) {
	MatrixDriver matrix;
	for (auto x : {-1, kDisplayWidth + kSideBarWidth})
		CHECK(matrix.padAction(x, 0, 100) == ActionResult::DEALT_WITH);
	for (auto y : {-1, kDisplayHeight})
		CHECK(matrix.padAction(0, y, 100) == ActionResult::DEALT_WITH);
	stemExport.processStarted = true;
	CHECK(matrix.padAction(0, 0, 100) == ActionResult::DEALT_WITH);
	CHECK_FALSE(matrix.isPadPressed(0, 0));
	LONGS_EQUAL(0, pad_calls);
}

TEST(ButtonDispatch, physical_pad_song_reuse_preserves_callback_modifier_state_and_skips_led_update) {
	session::Scope remote(session::Id::Remote);
	pic_available = true;
	pic_response = static_cast<PIC::Response>(20);
	on_pad = [] {
		song.~Song();
		new (&song) Song;
		Buttons::state().considerShiftReleaseForSticky = true;
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	};
	CHECK(readButtonsAndPads());
	CHECK(session::current() == session::Id::Remote);
	LONGS_EQUAL(1, pad_calls);
	LONGS_EQUAL(1, sticky_changes);
	CHECK(Buttons::states.for_owner(session::Id::Local).considerShiftReleaseForSticky);
	LONGS_EQUAL(0, shift_led_updates);
	LONGS_EQUAL(0, pic_replays);
}

TEST(ButtonDispatch, physical_routing_callback_invalidation_prevents_ui_dispatch) {
	for (int invalidation = 0; invalidation < 3; ++invalidation) {
		session::detail::active = session::Id::Local;
		current_uis.active() = &ui;
		pic_available = true;
		pic_response = static_cast<PIC::Response>(deluge::hid::button::PLAY);
		on_local_input = [=] {
			if (invalidation == 0) {
				song.~Song();
				new (&song) Song;
			}
			else if (invalidation == 1)
				current_uis.active() = &replacement_ui;
			else
				session::detail::active = session::Id::Remote;
		};
		CHECK(readButtonsAndPads());
		CHECK(session::current() == session::Id::Local);
		LONGS_EQUAL(0, button_calls);
		LONGS_EQUAL(0, play_calls);
	}
}

TEST(ButtonDispatch, physical_release_sweep_cancellation_skips_button_sweep) {
	pic_available = true;
	pic_response = PIC::Response::NO_PRESSES_HAPPENING;
	on_pad_release = [] {
		song.~Song();
		new (&song) Song;
	};
	CHECK(readButtonsAndPads());
	LONGS_EQUAL(1, pad_release_calls);
	LONGS_EQUAL(0, button_release_calls);
	LONGS_EQUAL(0, shift_led_updates);
}

TEST(ButtonDispatch, physical_valid_retry_restores_pic_edge_and_defers_until_storage_finishes) {
	pic_available = true;
	pic_response = static_cast<PIC::Response>(20);
	on_pad = [] { return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; };
	CHECK_FALSE(readButtonsAndPads());
	LONGS_EQUAL(1, pic_replays);
	CHECK(waitingForSDRoutineToEnd);
	LONGS_EQUAL(USE_DEFAULT_VELOCITY, nextPadPressIsOn);
	sdRoutineLock = true;
	CHECK_FALSE(readButtonsAndPads());
	LONGS_EQUAL(1, pad_calls);
}

TEST(ButtonDispatch, physical_mirror_consumption_does_not_call_local_ui) {
	pic_available = true;
	pic_response = static_cast<PIC::Response>(deluge::hid::button::PLAY);
	local_input_consumed = true;
	CHECK(readButtonsAndPads());
	LONGS_EQUAL(0, button_calls);
	LONGS_EQUAL(0, play_calls);
	LONGS_EQUAL(0, shift_led_updates);
}

TEST(ButtonDispatch, physical_pad_navigation_consumes_shift_before_changing_ui) {
	pic_available = true;
	pic_response = static_cast<PIC::Response>(20);
	Buttons::state().considerShiftReleaseForSticky = true;
	on_pad = [] {
		CHECK_FALSE(Buttons::state().considerShiftReleaseForSticky);
		current_uis.active() = &replacement_ui;
		return ActionResult::DEALT_WITH;
	};
	CHECK(readButtonsAndPads());
	LONGS_EQUAL(1, sticky_changes);
	LONGS_EQUAL(0, pic_replays);
}

TEST(ButtonDispatch, physical_protocol_messages_still_work_without_a_song) {
	currentSong = nullptr;
	pic_available = true;
	pic_response = PIC::Response::NEXT_PAD_OFF;
	CHECK(readButtonsAndPads());
	LONGS_EQUAL(0, nextPadPressIsOn);
	pic_available = true;
	pic_response = static_cast<PIC::Response>(deluge::hid::button::SELECT_ENC);
	CHECK(readButtonsAndPads());
	LONGS_EQUAL(1, button_calls);
	LONGS_EQUAL(USE_DEFAULT_VELOCITY, nextPadPressIsOn);
}
