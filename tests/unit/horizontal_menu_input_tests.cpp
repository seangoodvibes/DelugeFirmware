#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <map>
#include <optional>
namespace horizontal_menu_input_test {
namespace ui_session = deluge::gui::ui_session;
namespace hid {
using Button = int;
namespace button {
constexpr Button SYNTH = 0, KIT = 1, MIDI = 2, CV = 3, CROSS_SCREEN_EDIT = 4, SCALE_MODE = 5, SHIFT = 6, OTHER = 7;
}
} // namespace hid
namespace util {
template <class... T>
bool one_of(int value, T... options) {
	return ((value == options) || ...);
}
} // namespace util
static double now = 1000;
double getSystemTime() {
	return now;
}
namespace Buttons {
static bool shift;
bool isButtonPressed(hid::Button) {
	return shift;
}
} // namespace Buttons
struct SoundEditor {
	std::optional<int> chain;
	std::optional<int> getCurrentHorizontalMenusChain() { return chain; }
};
static SoundEditor editor;
SoundEditor& sound_editor_for_session() {
	return editor;
}
enum class ActionResult { DELEGATED, DEALT_WITH };
struct Submenu {
	int delegated = 0;
	ActionResult buttonAction(hid::Button, bool, bool) {
		++delegated;
		return ActionResult::DELEGATED;
	}
};
struct HorizontalMenu : Submenu {
	struct State {
		struct {
			int visiblePageItems = 0;
		} paging;
	} panel;
	int item = 0, selections = 0, pages = 0, chains = 0, last_selection = -1;
	State& horizontal_state() { return panel; }
	int* current_item_iterator() { return &item; }
	void switchHorizontalMenu(int direction, int) { chains += direction; }
	void switchVisiblePage(int direction) { pages += direction; }
	void handleInstrumentButtonPress(int, int, int index) {
		++selections;
		last_selection = index;
	}
	ActionResult buttonAction(hid::Button, bool, bool);
};
#include "horizontal_menu_input.inc"
TEST_GROUP(HorizontalMenuInput){void setup() override{now += 10;
Buttons::shift = false;
editor = {};
} // namespace horizontal_menu_input_test
}
;
TEST(HorizontalMenuInput, simultaneous_navigation_on_two_sessions_is_not_suppressed) {
	HorizontalMenu menu;
	{
		ui_session::Scope owner(ui_session::Id::Local);
		CHECK(menu.buttonAction(hid::button::SYNTH, true, false) == ActionResult::DEALT_WITH);
	}
	{
		ui_session::Scope owner(ui_session::Id::Remote);
		CHECK(menu.buttonAction(hid::button::KIT, true, false) == ActionResult::DEALT_WITH);
	}
	LONGS_EQUAL(2, menu.selections);
	LONGS_EQUAL(1, menu.last_selection);
	LONGS_EQUAL(0, menu.delegated);
}
TEST(HorizontalMenuInput, debounce_still_applies_across_menus_within_each_session) {
	for (auto owner : {ui_session::Id::Local, ui_session::Id::Remote}) {
		ui_session::Scope scope(owner);
		HorizontalMenu first, second;
		CHECK(first.buttonAction(hid::button::SYNTH, true, false) == ActionResult::DEALT_WITH);
		now += 0.05;
		CHECK(second.buttonAction(hid::button::CV, true, false) == ActionResult::DELEGATED);
		LONGS_EQUAL(0, second.selections);
		now += 0.1;
		CHECK(second.buttonAction(hid::button::CV, true, false) == ActionResult::DEALT_WITH);
		LONGS_EQUAL(3, second.last_selection);
	}
}
TEST(HorizontalMenuInput, page_and_shift_chain_navigation_are_isolated_between_sessions) {
	HorizontalMenu menu;
	{
		ui_session::Scope owner(ui_session::Id::Remote);
		CHECK(menu.buttonAction(hid::button::CROSS_SCREEN_EDIT, true, false) == ActionResult::DEALT_WITH);
		LONGS_EQUAL(1, menu.pages);
	}
	{
		ui_session::Scope owner(ui_session::Id::Local);
		Buttons::shift = true;
		editor.chain = 1;
		CHECK(menu.buttonAction(hid::button::SCALE_MODE, true, false) == ActionResult::DEALT_WITH);
		LONGS_EQUAL(-1, menu.chains);
	}
}
TEST(HorizontalMenuInput, releases_and_unrelated_buttons_do_not_consume_debounce_window) {
	ui_session::Scope owner(ui_session::Id::Remote);
	HorizontalMenu menu;
	CHECK(menu.buttonAction(hid::button::SYNTH, false, false) == ActionResult::DELEGATED);
	CHECK(menu.buttonAction(hid::button::OTHER, true, false) == ActionResult::DELEGATED);
	CHECK(menu.buttonAction(hid::button::MIDI, true, false) == ActionResult::DEALT_WITH);
	LONGS_EQUAL(2, menu.delegated);
	LONGS_EQUAL(2, menu.last_selection);
}
} // namespace horizontal_menu_input_test
