#include "CppUTest/TestHarness.h"
#include "gui/ui_timer_state.h"
#include "hid/led/pad_leds_state.h"
namespace pad_startup_reset_test {
namespace session = deluge::gui::ui_session;
using PadLEDs::PadState;
static session::State<PadState> panels;
static PadState& state() {
	return panels.active();
}
struct Timers {
	UITimerState timers;
	void unsetTimer(TimerName timer) { timers.unset(timer, 0); }
};
static Timers uiTimerManager;
void reset_transitions_for_session();
#include "pad_startup_reset.inc"
TEST_GROUP(PadStartupReset){};
TEST(PadStartupReset, resets_remote_transitions_without_clearing_local_or_retained_image_preferences) {
	panels = {};
	uiTimerManager = {};
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& panel = state();
		panel.zoomingIn = true;
		panel.zoomMagnitude = 3;
		panel.zoomPinSquare[0] = 7;
		panel.transitionTakingPlaceOnRow[0] = true;
		panel.explodeAnimationDirection = 1;
		panel.explodeAnimationTargetUI = reinterpret_cast<UI*>(&panel);
		panel.numAnimatedRows = 4;
		panel.greyProportion = 12;
		panel.greyoutChangeDirection = 1;
		panel.greyoutCols = panel.greyoutRows = 9;
		panel.transitionLength = panel.transitionStartTime = 123;
		panel.horizontal.squaresScrolled = panel.vertical.squaresScrolled = 2;
		panel.morphKeyboardSidebar = true;
		panel.slowFlashSquares[0] = 3;
		panel.slowFlashColours[0] = 2;
		panel.needToSendOutMainPadColours = panel.needToSendOutSidebarColours = true;
		panel.flashCursor = 2;
		panel.image[0][0] = RGB{10, 20, 30};
		uiTimerManager.timers.set(TimerName::MATRIX_DRIVER, 0, 50);
	}
	{
		session::Scope scope(session::Id::Remote);
		reset_transitions_for_session();
		reset_transitions_for_session();
		auto& panel = state();
		CHECK_FALSE(panel.zoomingIn);
		LONGS_EQUAL(0, panel.zoomMagnitude);
		LONGS_EQUAL(0, panel.zoomPinSquare[0]);
		CHECK_FALSE(panel.transitionTakingPlaceOnRow[0]);
		LONGS_EQUAL(0, panel.explodeAnimationDirection);
		POINTERS_EQUAL(nullptr, panel.explodeAnimationTargetUI);
		LONGS_EQUAL(0, panel.numAnimatedRows);
		LONGS_EQUAL(0, panel.greyProportion);
		LONGS_EQUAL(0, panel.greyoutChangeDirection);
		LONGS_EQUAL(0, panel.greyoutCols | panel.greyoutRows);
		LONGS_EQUAL(0, panel.transitionLength | panel.transitionStartTime);
		LONGS_EQUAL(0, panel.horizontal.squaresScrolled | panel.vertical.squaresScrolled);
		CHECK_FALSE(panel.morphKeyboardSidebar);
		for (auto square : panel.slowFlashSquares)
			LONGS_EQUAL(255, square);
		for (auto colour : panel.slowFlashColours)
			LONGS_EQUAL(0, colour);
		CHECK_FALSE(panel.needToSendOutMainPadColours);
		CHECK_FALSE(panel.needToSendOutSidebarColours);
		CHECK_FALSE(uiTimerManager.timers.get(TimerName::MATRIX_DRIVER).active);
		LONGS_EQUAL(2, panel.flashCursor);
		CHECK(panel.image[0][0] == (RGB{10, 20, 30}));
	}
	{
		session::Scope scope(session::Id::Local);
		auto& panel = state();
		CHECK_TRUE(panel.zoomingIn);
		LONGS_EQUAL(4, panel.numAnimatedRows);
		POINTERS_EQUAL(reinterpret_cast<UI*>(&panel), panel.explodeAnimationTargetUI);
		LONGS_EQUAL(9, panel.greyoutCols);
		LONGS_EQUAL(3, panel.slowFlashSquares[0]);
		CHECK_TRUE(panel.needToSendOutMainPadColours);
		CHECK_TRUE(uiTimerManager.timers.get(TimerName::MATRIX_DRIVER).active);
	}
}
} // namespace pad_startup_reset_test
