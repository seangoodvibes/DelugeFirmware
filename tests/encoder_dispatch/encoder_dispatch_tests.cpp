#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "hid/encoder_input_bank.h"
#include "util/lifetime.h"
#include <algorithm>
#include <functional>
#include <new>

namespace session = deluge::gui::ui_session;
namespace encoders = deluge::hid::encoders;
enum class ActionResult { DEALT_WITH, REMIND_ME_OUTSIDE_CARD_ROUTINE };
enum class RuntimeFeatureSettingType { Quantize };
enum class RuntimeFeatureStateToggle { Off, On };
constexpr int UI_MODE_LOADING_SONG_UNESSENTIAL_SAMPLES_ARMED = 1;
int currentUIMode = 0;
bool sdRoutineLock = false;
struct {
	bool processStarted = false;
} stemExport;
struct {
	RuntimeFeatureStateToggle get(RuntimeFeatureSettingType) { return RuntimeFeatureStateToggle::Off; }
} runtimeFeatureSettings;
struct Song {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch(lifetime); }
	void changeThresholdRecordingMode(int8_t) {}
};
Song song;
Song* currentSong = &song;
std::function<ActionResult()> on_function;
std::function<void()> on_mod;
int function_calls = 0, mod_calls = 0, activity_calls = 0;
struct UI {
	ActionResult horizontalEncoderAction(int32_t) {
		++function_calls;
		return on_function ? on_function() : ActionResult::DEALT_WITH;
	}
	ActionResult verticalEncoderAction(int32_t delta, bool) { return horizontalEncoderAction(delta); }
	void selectEncoderAction(int8_t delta) { horizontalEncoderAction(delta); }
	void tempoEncoderAction(int8_t delta, bool, bool) { horizontalEncoderAction(delta); }
	bool inNoteEditor() { return false; }
	void modEncoderAction(int32_t, int8_t) {
		++mod_calls;
		if (on_mod)
			on_mod();
	}
} ui, replacement_ui, clip_ui, automation_ui, playbackHandler;
session::State<UI*> current_uis;
UI* getCurrentUI() {
	return current_uis.active();
}
UI& instrument_clip_view_for_session() {
	return clip_ui;
}
UI& automation_view_for_session() {
	return automation_ui;
}
namespace deluge::hid::button {
enum { LEARN, TEMPO_ENC, CLIP_VIEW, RECORD };
}
namespace Buttons {
bool isShiftButtonPressed() {
	return false;
}
bool isButtonPressed(int) {
	return false;
}
} // namespace Buttons
namespace PadLEDs {
void changeDimmerInterval(int32_t) {
}
void changeRefreshTime(int8_t) {
}
} // namespace PadLEDs
namespace deluge::hid::display::Screensaver {
void noteActivity() {
	++activity_calls;
}
} // namespace deluge::hid::display::Screensaver
namespace deluge::hid::mirror {
bool client = false;
bool is_client() {
	return client;
}
} // namespace deluge::hid::mirror
namespace deluge::hid::encoders {
double ContinuousEncoder::calcNextKnobSpeed(int8_t) {
	return 1;
}
#include "encoder_dispatch.inc"
} // namespace deluge::hid::encoders

TEST_GROUP(EncoderDispatch) {
	encoders::EncoderInputBank bank;
	encoders::DetentedEncoder* functions[4];
	encoders::ContinuousEncoder* mods[2];
	void setup() override {
		session::detail::active = session::Id::Local;
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			current_uis.for_owner(owner) = &ui;
			encoders::input_states.for_owner(owner) = {};
		}
		for (int i = 0; i < 4; ++i)
			functions[i] = &bank.functions[i];
		for (int i = 0; i < 2; ++i)
			mods[i] = &bank.mods[i];
		function_calls = mod_calls = activity_calls = 0;
		on_function = {};
		on_mod = {};
		currentSong = &song;
		deluge::hid::mirror::client = false;
		sdRoutineLock = false;
		currentUIMode = 0;
	}
	void teardown() override {
		on_function = {};
		on_mod = {};
		currentSong = &song;
		session::detail::active = session::Id::Local;
	}
	bool dispatch(bool skip = false) {
		return encoders::interpret_encoder_bank(functions, mods, skip);
	}
};

TEST(EncoderDispatch, invalidated_function_callback_does_not_restore_retry_or_consume_later_ticks) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		for (int encoder_index : {0, 1}) {
			for (int invalidation = 0; invalidation < 4; ++invalidation) {
				session::Scope scope(owner);
				current_uis.active() = &ui;
				deluge::hid::mirror::client = false;
				bank.clear();
				bank.functions[encoder_index].restore(1);
				bank.functions[3].restore(2);
				on_function = [=] {
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
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				};
				const int previous_calls = function_calls;
				CHECK(dispatch());
				CHECK(session::current() == owner);
				LONGS_EQUAL(previous_calls + 1, function_calls);
				CHECK_FALSE(bank.functions[encoder_index].pending());
				LONGS_EQUAL(2, bank.functions[3].take());
				LONGS_EQUAL(0, encoders::input_state().waiting_for_card_routine_end);
			}
		}
	}
}

TEST(EncoderDispatch, mod_callback_invalidation_preserves_input_state_and_later_ticks) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		bank.mods[0].apply_edges(2);
		bank.mods[1].apply_edges(3);
		encoders::input_state().initial_turn_direction[0] = 1;
		on_mod = [] {
			song.~Song();
			new (&song) Song;
		};
		const int previous_calls = mod_calls;
		CHECK(dispatch());
		LONGS_EQUAL(previous_calls + 1, mod_calls);
		LONGS_EQUAL(1, encoders::input_state().initial_turn_direction[0]);
		LONGS_EQUAL(3, bank.mods[1].take());
	}
}

TEST(EncoderDispatch, unchanged_context_restores_deferred_ticks_and_services_other_encoders) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		bank.functions[0].restore(2);
		bank.mods[0].apply_edges(1);
		on_function = [] { return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE; };
		CHECK(dispatch());
		LONGS_EQUAL(2, bank.functions[0].take());
		LONGS_EQUAL(1, encoders::input_state().waiting_for_card_routine_end);
		CHECK_FALSE(bank.mods[0].pending());
	}
}

TEST(EncoderDispatch, retired_song_keeps_pending_input_untouched) {
	Song retired_song;
	retired_song.lifetime.retire();
	currentSong = &retired_song;
	bank.functions[0].restore(1);
	CHECK_FALSE(dispatch());
	CHECK(bank.functions[0].pending());
	LONGS_EQUAL(0, function_calls);
}

TEST(EncoderDispatch, missing_ui_or_client_takeover_leaves_ticks_pending) {
	for (bool client : {false, true}) {
		current_uis.active() = client ? &ui : nullptr;
		deluge::hid::mirror::client = client;
		bank.clear();
		bank.functions[0].restore(1);
		bank.mods[0].apply_edges(1);
		CHECK_FALSE(dispatch());
		CHECK(bank.functions[0].pending());
		CHECK(bank.mods[0].pending());
	}
	LONGS_EQUAL(0, function_calls);
	LONGS_EQUAL(0, mod_calls);
}

TEST(EncoderDispatch, no_song_menu_keeps_working) {
	currentSong = nullptr;
	bank.functions[3].restore(1);
	CHECK(dispatch());
	LONGS_EQUAL(1, function_calls);
	LONGS_EQUAL(1, activity_calls);
}

TEST(EncoderDispatch, card_routine_keeps_function_and_mod_ticks_deferred) {
	sdRoutineLock = true;
	bank.functions[1].restore(2);
	bank.mods[0].apply_edges(3);
	CHECK_FALSE(dispatch());
	LONGS_EQUAL(2, bank.functions[1].take());
	LONGS_EQUAL(3, bank.mods[0].take());
	LONGS_EQUAL(0, function_calls);
	LONGS_EQUAL(0, mod_calls);
}

int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
