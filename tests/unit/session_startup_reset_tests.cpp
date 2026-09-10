#include "CppUTest/TestHarness.h"
#include <cstdint>
namespace session_startup_reset_test {
struct SoundEditor {
	int resets = 0;
	void reset_for_session_startup() { ++resets; }
};
static SoundEditor sound_editor;
static SoundEditor& sound_editor_for_session() {
	return sound_editor;
}
struct View {
	int learn_resets = 0, modulation_resets = 0;
	void reset_modulation_for_session_startup() { ++modulation_resets; }
	void reset_midi_learn_for_session_startup() { ++learn_resets; }
};
static View view_state;
static View& view_for_session() {
	return view_state;
}
struct InstrumentClipView {
	int selection_resets = 0;
	void reset_selection_display_for_session_startup() { ++selection_resets; }
};
InstrumentClipView instrument_view;
InstrumentClipView& instrument_clip_view_for_session() {
	return instrument_view;
}
struct AutomationView {
	int gesture_resets = 0;
	void reset_gestures_for_session_startup() { ++gesture_resets; }
	int resets = 0;
	void reset_navigation_for_session_startup() { ++resets; }
};
AutomationView automation_view;
AutomationView& automation_view_for_session() {
	return automation_view;
}
struct SessionView {
	bool performActionOnPadRelease = true, performActionOnSectionPadRelease = true;
	bool clipWasSelectedWithShift = true, sessionButtonActive = true, sessionButtonUsed = true;
	bool horizontalEncoderPressed = true, viewingRecordArmingActive = true, createClip = true;
	uint8_t selectedClipYDisplay = 2, selectedClipPressYDisplay = 3, selectedClipPressXDisplay = 4;
	uint32_t selectedClipTimePressed = 17;
	int pulse_stops = 0, press_resets = 0;
	void gridStopSelectedClipPulsing() { ++pulse_stops; }
	void gridResetPresses() { ++press_resets; }
	void reset_for_remote_startup();
};
#include "session_startup_reset.inc"
TEST_GROUP(SessionStartupReset){};
TEST(SessionStartupReset, production_reset_cancels_retained_gestures_and_selected_pad) {
	instrument_view = {};
	automation_view = {};
	sound_editor = {};
	view_state = {};
	SessionView view;
	view.reset_for_remote_startup();
	CHECK_FALSE(view.performActionOnPadRelease);
	CHECK_FALSE(view.performActionOnSectionPadRelease);
	CHECK_FALSE(view.clipWasSelectedWithShift);
	CHECK_FALSE(view.sessionButtonActive);
	CHECK_FALSE(view.sessionButtonUsed);
	CHECK_FALSE(view.horizontalEncoderPressed);
	CHECK_FALSE(view.viewingRecordArmingActive);
	CHECK_FALSE(view.createClip);
	LONGS_EQUAL(255, view.selectedClipYDisplay);
	LONGS_EQUAL(255, view.selectedClipPressYDisplay);
	LONGS_EQUAL(255, view.selectedClipPressXDisplay);
	UNSIGNED_LONGS_EQUAL(0, view.selectedClipTimePressed);
	LONGS_EQUAL(1, sound_editor.resets);
	LONGS_EQUAL(1, automation_view.resets);
	LONGS_EQUAL(1, instrument_view.selection_resets);
	LONGS_EQUAL(1, automation_view.gesture_resets);
	LONGS_EQUAL(1, view_state.learn_resets);
	LONGS_EQUAL(1, view_state.modulation_resets);
	LONGS_EQUAL(1, view.pulse_stops);
	LONGS_EQUAL(1, view.press_resets);
}
} // namespace session_startup_reset_test
