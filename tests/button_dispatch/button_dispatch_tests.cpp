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
enum class TimerName { BACK_MENU_EXIT };
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
std::function<ActionResult()> on_button;
std::function<void()> on_play, on_record, on_popup;
int button_calls = 0, play_calls = 0, record_calls = 0, mod_calls = 0;
struct UI {
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
TEST_GROUP(ButtonDispatch){void setup() override{session::detail::active = session::Id::Local;
for (auto owner : {session::Id::Local, session::Id::Remote}) {
	current_uis.for_owner(owner) = &ui;
	Buttons::states.for_owner(owner) = {};
	recorders.for_owner(owner) = {};
}
on_button = {};
on_play = on_record = on_popup = {};
button_calls = play_calls = record_calls = mod_calls = 0;
currentSong = &song;
deluge::hid::mirror::client = false;
}
void teardown() override {
	on_button = {};
	on_play = on_record = on_popup = {};
	currentSong = &song;
	session::detail::active = session::Id::Local;
}
}
;

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
