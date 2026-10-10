#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <functional>
#include <vector>
namespace session = deluge::gui::ui_session;
std::vector<session::Id> constructed_owners, destroyed_owners;
struct Display {
	bool oled;
	explicit Display(bool is_oled) : oled(is_oled) { constructed_owners.push_back(session::current()); }
	virtual ~Display() { destroyed_owners.push_back(session::current()); }
	bool haveOLED() const { return oled; }
};
Display* display = nullptr;
namespace deluge::l10n {
inline void* chosenLanguage = nullptr;
}
namespace deluge::hid::display {
struct OLED : Display {
	OLED() : Display(true) {}
};
struct SevenSegment : Display {
	SevenSegment() : Display(false) {}
};
} // namespace deluge::hid::display
struct UI {
	int changed = 0, focused = 0;
	std::function<void()> on_changed;
	void displayOrLanguageChanged() {
		++changed;
		if (on_changed)
			on_changed();
	}
	void focusRegained() { ++focused; }
};
session::State<UI*> current_uis;
session::State<int> renders;
UI* getCurrentUI() {
	return current_uis.active();
}
void doAnyPendingUIRendering() {
	++renders.active();
}
namespace deluge::hid::display {
#include "display_swap.inc"
}
using deluge::hid::display::swapDisplayType;
TEST_GROUP(DisplaySwap) {
	UI local_ui, remote_ui, replacement_ui;
	void setup() override {
		session::detail::active = session::Id::Local;
		current_uis.for_owner(session::Id::Local) = &local_ui;
		current_uis.for_owner(session::Id::Remote) = &remote_ui;
		renders = {};
		constructed_owners.clear();
		destroyed_owners.clear();
	}
	void teardown() override {
		delete display;
		display = nullptr;
		session::detail::active = session::Id::Local;
	}
	void initial_display(bool oled) {
		display = oled ? static_cast<Display*>(new deluge::hid::display::OLED) : new deluge::hid::display::SevenSegment;
		constructed_owners.clear();
		destroyed_owners.clear();
	}
};
TEST(DisplaySwap, remote_setting_switches_and_refreshes_physical_local_display) {
	for (bool oled : {true, false}) {
		initial_display(oled);
		local_ui.changed = local_ui.focused = remote_ui.changed = remote_ui.focused = 0;
		renders = {};
		{
			session::Scope remote(session::Id::Remote);
			swapDisplayType();
			CHECK(session::current() == session::Id::Remote);
		}
		CHECK(display->haveOLED() != oled);
		LONGS_EQUAL(1, constructed_owners.size());
		CHECK(constructed_owners[0] == session::Id::Local);
		LONGS_EQUAL(1, destroyed_owners.size());
		CHECK(destroyed_owners[0] == session::Id::Local);
		LONGS_EQUAL(1, local_ui.changed);
		LONGS_EQUAL(oled ? 1 : 0, local_ui.focused);
		LONGS_EQUAL(0, remote_ui.changed);
		LONGS_EQUAL(0, remote_ui.focused);
		LONGS_EQUAL(1, renders.for_owner(session::Id::Local));
		LONGS_EQUAL(0, renders.for_owner(session::Id::Remote));
		delete display;
		display = nullptr;
	}
}
TEST(DisplaySwap, callback_replacing_current_ui_focuses_the_new_ui) {
	initial_display(true);
	local_ui.on_changed = [&] { current_uis.active() = &replacement_ui; };
	swapDisplayType();
	LONGS_EQUAL(1, local_ui.changed);
	LONGS_EQUAL(0, local_ui.focused);
	LONGS_EQUAL(1, replacement_ui.focused);
}
TEST(DisplaySwap, callback_removing_ui_does_not_focus_the_old_ui) {
	initial_display(true);
	local_ui.on_changed = [&] { current_uis.active() = nullptr; };
	swapDisplayType();
	LONGS_EQUAL(0, local_ui.focused);
	LONGS_EQUAL(1, renders.active());
}
TEST(DisplaySwap, local_switch_and_missing_ui_still_render_the_local_panel) {
	for (bool oled : {true, false}) {
		initial_display(oled);
		swapDisplayType();
		CHECK(display->haveOLED() != oled);
		CHECK(session::current() == session::Id::Local);
		delete display;
		display = nullptr;
	}
	LONGS_EQUAL(2, local_ui.changed);
	LONGS_EQUAL(1, local_ui.focused);
	current_uis.active() = nullptr;
	initial_display(true);
	swapDisplayType();
	LONGS_EQUAL(3, renders.active());
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
