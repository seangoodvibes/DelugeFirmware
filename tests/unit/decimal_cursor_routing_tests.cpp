#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <cstdint>
#include <functional>
namespace decimal_cursor_routing_test {
namespace session = deluge::gui::ui_session;
struct Editor {
	int numberEditPos = 1, numberEditSize = 10;
};
static session::State<Editor> editors;
Editor& sound_editor_for_session() {
	return editors.active();
}
struct Display {
	bool oled = true;
	bool haveOLED() { return oled; }
} screen;
static Display* display = &screen;
static std::function<void()> render;
void renderUIsForOled() {
	if (render)
		render();
}
struct Decimal {
	int scrolls = 0, draws = 0;
	int getMaxValue() { return 100; }
	void scrollToGoodPos() { ++scrolls; }
	void drawActualValue(bool) { ++draws; }
	void horizontalEncoderAction(int32_t offset);
};
#include "decimal_cursor_routing.inc"
TEST_GROUP(DecimalCursorRouting){void setup() override{editors = {};
screen.oled = true;
render = {};
for (auto owner : {session::Id::Local, session::Id::Remote}) {
	session::Scope scope(owner);
	moving_cursor_for_session() = false;
}
} // namespace decimal_cursor_routing_test
void teardown() override {
	render = {};
}
}
;
TEST(DecimalCursorRouting, nested_remote_render_does_not_see_or_clear_local_cursor_movement) {
	Decimal menu;
	int depth = 0;
	render = [&] {
		CHECK_TRUE(moving_cursor_for_session());
		if (depth++ == 0) {
			{
				session::Scope remote(session::Id::Remote);
				CHECK_FALSE(moving_cursor_for_session());
				menu.horizontalEncoderAction(-1);
				CHECK_FALSE(moving_cursor_for_session());
			}
			CHECK_TRUE(moving_cursor_for_session());
		}
	};
	session::Scope local(session::Id::Local);
	menu.horizontalEncoderAction(1);
	CHECK_FALSE(moving_cursor_for_session());
	LONGS_EQUAL(0, editors.for_owner(session::Id::Local).numberEditPos);
	LONGS_EQUAL(2, editors.for_owner(session::Id::Remote).numberEditPos);
	LONGS_EQUAL(2, depth);
}
TEST(DecimalCursorRouting, nested_same_session_edit_restores_outer_cursor_movement) {
	Decimal menu;
	int depth = 0;
	render = [&] {
		CHECK_TRUE(moving_cursor_for_session());
		if (depth++ == 0) {
			menu.horizontalEncoderAction(-1);
			CHECK_TRUE(moving_cursor_for_session());
		}
	};
	session::Scope remote(session::Id::Remote);
	menu.horizontalEncoderAction(1);
	CHECK_FALSE(moving_cursor_for_session());
	LONGS_EQUAL(2, depth);
}
TEST(DecimalCursorRouting, numeric_display_updates_digit_without_activating_oled_cursor_flag) {
	screen.oled = false;
	Decimal menu;
	session::Scope remote(session::Id::Remote);
	menu.horizontalEncoderAction(1);
	menu.horizontalEncoderAction(1);
	LONGS_EQUAL(0, sound_editor_for_session().numberEditPos);
	LONGS_EQUAL(1, sound_editor_for_session().numberEditSize);
	CHECK_FALSE(moving_cursor_for_session());
	LONGS_EQUAL(2, menu.scrolls);
	LONGS_EQUAL(2, menu.draws);
}
} // namespace decimal_cursor_routing_test
