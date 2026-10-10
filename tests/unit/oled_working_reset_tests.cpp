#include "CppUTest/TestHarness.h"
#include "gui/ui_timer_state.h"
namespace oled_working_reset_test {
namespace session = deluge::gui::ui_session;
enum class PopupType { NOTIFICATION };
struct Panel {
	int working_animation_count = 0;
	bool notification = false;
	int numConsoleItems = 0, consoleMinX = -1, consoleMaxX = 0;
	bool dirty = false;
};
static session::State<Panel> panels;
static Panel& panel_state() {
	return panels.active();
}
struct Timers {
	UITimerState state;
	void unsetTimer(TimerName timer) { state.unset(timer, 0); }
};
static Timers uiTimerManager;
struct OLED {
	bool hasPopupOfType(PopupType) { return panel_state().notification; }
	void removePopup() { panel_state().notification = false; }
	void removeWorkingAnimation();
	static void clear_console_for_session();
	static void markChanged() { panel_state().dirty = true; }
};
#include "oled_working_reset.inc"
TEST_GROUP(OLEDWorkingReset){};
TEST(OLEDWorkingReset, production_cleanup_stops_remote_animation_and_preserves_local) {
	uiTimerManager = {};
	panels.for_owner(session::Id::Local) = {3, true};
	panels.for_owner(session::Id::Remote) = {7, true};
	{
		session::Scope local(session::Id::Local);
		uiTimerManager.state.set(TimerName::LOADING_ANIMATION, 0, 50);
	}
	{
		session::Scope remote(session::Id::Remote);
		uiTimerManager.state.set(TimerName::LOADING_ANIMATION, 0, 20);
		OLED{}.removeWorkingAnimation();
		LONGS_EQUAL(0, panel_state().working_animation_count);
		CHECK_FALSE(panel_state().notification);
		CHECK_FALSE(uiTimerManager.state.get(TimerName::LOADING_ANIMATION).active);
	}
	{
		session::Scope local(session::Id::Local);
		LONGS_EQUAL(3, panel_state().working_animation_count);
		CHECK_TRUE(panel_state().notification);
		CHECK_TRUE(uiTimerManager.state.get(TimerName::LOADING_ANIMATION).active);
		LONGS_EQUAL(50, uiTimerManager.state.get(TimerName::LOADING_ANIMATION).triggerTime);
	}
}
} // namespace oled_working_reset_test

namespace oled_working_reset_test {
TEST(OLEDWorkingReset, production_console_clear_removes_remote_entries_and_timer_only) {
	uiTimerManager = {};
	panels.for_owner(session::Id::Local) = {};
	panels.for_owner(session::Id::Remote) = {};
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		panel_state().numConsoleItems = 3;
		panel_state().consoleMinX = 4;
		panel_state().consoleMaxX = 124;
		uiTimerManager.state.set(TimerName::OLED_CONSOLE, 0, 50);
	}
	{
		session::Scope remote(session::Id::Remote);
		OLED::clear_console_for_session();
		LONGS_EQUAL(0, panel_state().numConsoleItems);
		LONGS_EQUAL(-1, panel_state().consoleMinX);
		LONGS_EQUAL(0, panel_state().consoleMaxX);
		CHECK_TRUE(panel_state().dirty);
		CHECK_FALSE(uiTimerManager.state.get(TimerName::OLED_CONSOLE).active);
	}
	{
		session::Scope local(session::Id::Local);
		LONGS_EQUAL(3, panel_state().numConsoleItems);
		LONGS_EQUAL(4, panel_state().consoleMinX);
		LONGS_EQUAL(124, panel_state().consoleMaxX);
		CHECK_FALSE(panel_state().dirty);
		CHECK_TRUE(uiTimerManager.state.get(TimerName::OLED_CONSOLE).active);
	}
}
} // namespace oled_working_reset_test
