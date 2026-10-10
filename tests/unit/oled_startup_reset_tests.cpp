#include "CppUTest/TestHarness.h"
#include "gui/ui_timer_state.h"
#include <string>

// The timer register is unrelated to reset behavior and unavailable on native hosts.
#define ENABLE_TEXT_OUTPUT 0
#define NUM_SIDE_SCROLLERS 2
namespace oled_startup_reset_test {
namespace session = deluge::gui::ui_session;
struct StoredText {
	std::string value;
	void clear() { value.clear(); }
};
struct SideScroller {
	StoredText string_;
	const char* text = nullptr;
};
struct Panel {
	struct {
		uint32_t u32 = 0;
	} blinkArea;
	int sideScrollerDirection = 0;
	SideScroller sideScrollers[2];
	bool drawnPermanentPopup = false;
	bool dirty = false;
	struct Canvas {
		bool has_old_pixels = false;
		int clears = 0;
		void clear() {
			has_old_pixels = false;
			++clears;
		}
	} canvas;
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
	void reset_layers_for_session();
	static void clearMainImage();
	static void stopBlink();
	static void stopScrollingAnimation();
	static auto& main_for_session() { return panel_state().canvas; }
	static void markChanged() { panel_state().dirty = true; }
};
#include "oled_startup_reset.inc"
TEST_GROUP(OLEDStartupReset){void setup() override{panels = {};
uiTimerManager = {};
} // namespace oled_startup_reset_test
}
;
TEST(OLEDStartupReset, discards_remote_blink_scroll_text_and_canvas_preserving_local) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& panel = panel_state();
		panel.blinkArea.u32 = 123;
		panel.sideScrollerDirection = -1;
		panel.drawnPermanentPopup = true;
		panel.canvas.has_old_pixels = true;
		for (auto& scroller : panel.sideScrollers) {
			scroller.string_.value = "old menu";
			scroller.text = scroller.string_.value.c_str();
		}
		uiTimerManager.state.set(TimerName::OLED_SCROLLING_AND_BLINKING, 0, 50);
	}
	{
		session::Scope scope(session::Id::Remote);
		OLED{}.reset_layers_for_session();
		auto& panel = panel_state();
		LONGS_EQUAL(0, panel.blinkArea.u32);
		LONGS_EQUAL(0, panel.sideScrollerDirection);
		CHECK_FALSE(panel.drawnPermanentPopup);
		CHECK_FALSE(panel.canvas.has_old_pixels);
		CHECK_TRUE(panel.dirty);
		LONGS_EQUAL(1, panel.canvas.clears);
		for (auto& scroller : panel.sideScrollers) {
			CHECK(scroller.string_.value.empty());
			POINTERS_EQUAL(nullptr, scroller.text);
		}
		CHECK_FALSE(uiTimerManager.state.get(TimerName::OLED_SCROLLING_AND_BLINKING).active);
		OLED{}.reset_layers_for_session();
		LONGS_EQUAL(2, panel.canvas.clears);
	}
	{
		session::Scope scope(session::Id::Local);
		auto& panel = panel_state();
		LONGS_EQUAL(123, panel.blinkArea.u32);
		LONGS_EQUAL(-1, panel.sideScrollerDirection);
		CHECK_TRUE(panel.drawnPermanentPopup);
		CHECK_TRUE(panel.canvas.has_old_pixels);
		CHECK_FALSE(panel.dirty);
		LONGS_EQUAL(0, panel.canvas.clears);
		for (auto& scroller : panel.sideScrollers)
			STRCMP_EQUAL("old menu", scroller.text);
		CHECK_TRUE(uiTimerManager.state.get(TimerName::OLED_SCROLLING_AND_BLINKING).active);
	}
}
} // namespace oled_startup_reset_test
#undef NUM_SIDE_SCROLLERS
#undef ENABLE_TEXT_OUTPUT
