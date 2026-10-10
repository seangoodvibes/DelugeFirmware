#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <array>
namespace automation_gesture_reset_test {
namespace session = deluge::gui::ui_session;
constexpr int kNoSelection = 255;
enum class TimerName { SHORTCUT_BLINK, INTERPOLATION_SHORTCUT_BLINK, PAD_SELECTION_SHORTCUT_BLINK };
struct timers {
	session::State<std::array<bool, 3>> active;
	void unsetTimer(TimerName timer) { active.active()[static_cast<int>(timer)] = false; }
} uiTimerManager;
struct AutomationView {
	bool padSelectionOn = true, multiPadPressSelected = true, multiPadPressActive = true, middlePadPressSelected = true;
	bool interpolationBefore = true, interpolationAfter = true, interpolation = true;
	bool parameterShortcutBlinking = true, interpolationShortcutBlinking = true, padSelectionShortcutBlinking = true;
	bool probabilityChanged = true;
	int leftPadSelectedX = 1, leftPadSelectedY = 2, rightPadSelectedX = 3, rightPadSelectedY = 4;
	int lastPadSelectedKnobPos = 12;
	uint32_t timeSelectKnobLastReleased = 123;
	void reset_gestures_for_session_startup();
	void initPadSelection();
	void initInterpolation();
	void resetParameterShortcutBlinking();
	void resetInterpolationShortcutBlinking();
	void resetPadSelectionShortcutBlinking();
};
#include "automation_gesture_reset.inc"
TEST_GROUP(AutomationGestureReset){};
TEST(AutomationGestureReset, startup_cancels_remote_gestures_and_timers_without_changing_local) {
	session::State<AutomationView> views;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		uiTimerManager.active.for_owner(owner).fill(true);
	}
	views.for_owner(session::Id::Remote).interpolation = false;
	session::Scope remote(session::Id::Remote);
	auto& view = views.active();
	view.reset_gestures_for_session_startup();
	view.reset_gestures_for_session_startup();
	CHECK(!view.padSelectionOn);
	CHECK(!view.multiPadPressSelected);
	CHECK(!view.multiPadPressActive);
	CHECK(!view.middlePadPressSelected);
	CHECK(!view.interpolationBefore);
	CHECK(!view.interpolationAfter);
	CHECK(!view.interpolation);
	CHECK(!view.parameterShortcutBlinking);
	CHECK(!view.interpolationShortcutBlinking);
	CHECK(!view.padSelectionShortcutBlinking);
	CHECK(!view.probabilityChanged);
	LONGS_EQUAL(0, view.timeSelectKnobLastReleased);
	for (int value : {view.leftPadSelectedX, view.leftPadSelectedY, view.rightPadSelectedX, view.rightPadSelectedY,
	                  view.lastPadSelectedKnobPos})
		LONGS_EQUAL(kNoSelection, value);
	for (bool active : uiTimerManager.active.active())
		CHECK(!active);
	auto& local = views.for_owner(session::Id::Local);
	CHECK(local.padSelectionOn);
	CHECK(local.multiPadPressSelected);
	CHECK(local.multiPadPressActive);
	CHECK(local.middlePadPressSelected);
	CHECK(local.interpolationBefore);
	CHECK(local.interpolationAfter);
	CHECK(local.interpolation);
	CHECK(local.parameterShortcutBlinking);
	CHECK(local.interpolationShortcutBlinking);
	CHECK(local.padSelectionShortcutBlinking);
	CHECK(local.probabilityChanged);
	LONGS_EQUAL(123, local.timeSelectKnobLastReleased);
	LONGS_EQUAL(1, local.leftPadSelectedX);
	LONGS_EQUAL(2, local.leftPadSelectedY);
	LONGS_EQUAL(3, local.rightPadSelectedX);
	LONGS_EQUAL(4, local.rightPadSelectedY);
	LONGS_EQUAL(12, local.lastPadSelectedKnobPos);
	for (bool active : uiTimerManager.active.for_owner(session::Id::Local))
		CHECK(active);
}
} // namespace automation_gesture_reset_test
