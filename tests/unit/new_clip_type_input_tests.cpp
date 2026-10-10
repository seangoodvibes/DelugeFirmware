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
TEST(NewClipTypeInput, button_releases_do_not_select_or_close_on_either_owner) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (auto button : {deluge::hid::button::SELECT_ENC, deluge::hid::button::SYNTH, deluge::hid::button::BACK}) {
			CHECK(menu.buttonAction(button, false, false) == ActionResult::DEALT_WITH);
			LONGS_EQUAL(0, panels.active().button_calls);
			LONGS_EQUAL(0, panels.active().closes);
			LONGS_EQUAL(0, panels.active().transitions);
		}
	}
}
TEST(NewClipTypeInput, unrelated_or_deferred_button_does_not_close_menu) {
	for (auto result : {ActionResult::NOT_DEALT_WITH, ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE}) {
		panels.active().result = result;
		CHECK(menu.buttonAction(deluge::hid::button::SHIFT, true, false) == result);
		LONGS_EQUAL(0, panels.active().closes);
		LONGS_EQUAL(0, panels.active().transitions);
	}
}
TEST(NewClipTypeInput, handled_button_press_dispatches_and_closes_initiating_panel) {
	session::Scope scope(session::Id::Remote);
	panels.active().result = ActionResult::ACTIONED_AND_CAUSED_CHANGE;
	CHECK(menu.buttonAction(deluge::hid::button::SYNTH, true, false) == ActionResult::DEALT_WITH);
	LONGS_EQUAL(1, panels.active().button_calls);
	CHECK(panels.active().last_on);
	LONGS_EQUAL(1, panels.active().closes);
	LONGS_EQUAL(1, panels.active().transitions);
	LONGS_EQUAL(0, panels.for_owner(session::Id::Local).button_calls);
	LONGS_EQUAL(0, panels.for_owner(session::Id::Local).closes);
}
