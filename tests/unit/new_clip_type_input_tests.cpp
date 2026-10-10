#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "hid/button.h"
namespace new_clip_input_test {
namespace session = deluge::gui::ui_session;
struct panel_state {
	int closes = 0, transitions = 0, pad_calls = 0, button_calls = 0;
	bool last_on = false;
	ActionResult result = ActionResult::DEALT_WITH;
};
static session::State<panel_state> panels;
struct display_fixture {
	void setNextTransitionDirection(int) { ++panels.active().transitions; }
};
static display_fixture display_instance;
static auto* display = &display_instance;
struct session_view_fixture {
	ActionResult padAction(int32_t, int32_t, int32_t) {
		++panels.active().pad_calls;
		return panels.active().result;
	}
	ActionResult clipCreationButtonPressed(deluge::hid::Button, bool on, bool) {
		++panels.active().button_calls;
		panels.active().last_on = on;
		return panels.active().result;
	}
};
static session_view_fixture session_view;
static session_view_fixture& session_view_for_session() {
	return session_view;
}
static bool sdRoutineLock = false;
class NewClipType {
public:
	int32_t currentOption = 0;
	void close() { ++panels.active().closes; }
	bool acceptCurrentOption();
	ActionResult padAction(int32_t, int32_t, int32_t);
	ActionResult buttonAction(deluge::hid::Button, bool, bool);
};
#include "new_clip_type_input.inc"
} // namespace new_clip_input_test
using namespace new_clip_input_test;
TEST_GROUP(NewClipTypeInput) {
	NewClipType menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		panels = {};
		sdRoutineLock = false;
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(NewClipTypeInput, deferred_pad_keeps_menu_open_until_retry_on_each_owner) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& panel = panels.active();
		panel.result = ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		CHECK(menu.padAction(1, 2, 1) == panel.result);
		LONGS_EQUAL(0, panel.closes);
		LONGS_EQUAL(0, panel.transitions);
		panel.result = ActionResult::ACTIONED_AND_CAUSED_CHANGE;
		CHECK(menu.padAction(1, 2, 1) == panel.result);
		LONGS_EQUAL(2, panel.pad_calls);
		LONGS_EQUAL(1, panel.closes);
		LONGS_EQUAL(1, panel.transitions);
	}
}
