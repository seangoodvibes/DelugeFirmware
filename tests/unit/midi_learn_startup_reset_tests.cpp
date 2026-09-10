#include "CppUTest/TestHarness.h"
#include "gui/ui_timer_state.h"
namespace midi_learn_startup_reset_test {
namespace session = deluge::gui::ui_session;
enum class MidiLearn { NONE, DRUM_INPUT };
struct Target {
	int value = 7;
};
struct View {
	MidiLearn thingPressedForMidiLearn = MidiLearn::DRUM_INPUT;
	bool deleteMidiCommandOnRelease = true;
	bool midiLearnFlashOn = true;
	bool shouldSaveSettingsAfterMidiLearn = true;
	Target* learnedThing = nullptr;
	Target* instrumentPressedForMIDILearn = nullptr;
	Target* drumPressedForMIDILearn = nullptr;
	Target* kitPressedForMIDILearn = nullptr;
	void reset_midi_learn_for_session_startup();
};
struct Timers {
	UITimerState state;
	void unsetTimer(TimerName timer) { state.unset(timer, 0); }
};
static Timers uiTimerManager;
#include "midi_learn_startup_reset.inc"
TEST_GROUP(MidiLearnStartupReset){};
TEST(MidiLearnStartupReset, drops_remote_targets_without_editing_them_or_losing_pending_settings_save) {
	session::State<View> views;
	Target target;
	uiTimerManager = {};
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = views.active();
		view.learnedThing = view.instrumentPressedForMIDILearn = &target;
		view.drumPressedForMIDILearn = view.kitPressedForMIDILearn = &target;
		uiTimerManager.state.set(TimerName::MIDI_LEARN_FLASH, 0, 50);
	}
	{
		session::Scope scope(session::Id::Remote);
		auto& view = views.active();
		view.reset_midi_learn_for_session_startup();
		view.reset_midi_learn_for_session_startup();
		CHECK(view.thingPressedForMidiLearn == MidiLearn::NONE);
		CHECK_FALSE(view.deleteMidiCommandOnRelease);
		CHECK_FALSE(view.midiLearnFlashOn);
		POINTERS_EQUAL(nullptr, view.learnedThing);
		POINTERS_EQUAL(nullptr, view.instrumentPressedForMIDILearn);
		POINTERS_EQUAL(nullptr, view.drumPressedForMIDILearn);
		POINTERS_EQUAL(nullptr, view.kitPressedForMIDILearn);
		CHECK_TRUE(view.shouldSaveSettingsAfterMidiLearn);
		CHECK_FALSE(uiTimerManager.state.get(TimerName::MIDI_LEARN_FLASH).active);
	}
	{
		session::Scope scope(session::Id::Local);
		auto& view = views.active();
		CHECK(view.thingPressedForMidiLearn == MidiLearn::DRUM_INPUT);
		CHECK_TRUE(view.deleteMidiCommandOnRelease);
		CHECK_TRUE(view.midiLearnFlashOn);
		POINTERS_EQUAL(&target, view.learnedThing);
		POINTERS_EQUAL(&target, view.instrumentPressedForMIDILearn);
		POINTERS_EQUAL(&target, view.drumPressedForMIDILearn);
		POINTERS_EQUAL(&target, view.kitPressedForMIDILearn);
		CHECK_TRUE(uiTimerManager.state.get(TimerName::MIDI_LEARN_FLASH).active);
	}
	LONGS_EQUAL(7, target.value);
}
} // namespace midi_learn_startup_reset_test
