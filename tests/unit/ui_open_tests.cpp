#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <array>
#include <functional>
namespace ui_open_test {
namespace session = ::deluge::gui::ui_session;
namespace deluge::gui {
namespace ui_session = ::deluge::gui::ui_session;
}
struct UI {
	UI* redirected = this;
	bool success = true;
	int opens = 0, focuses = 0;
	std::function<void()> on_open, on_resolve, on_focus;
	UI* getUI() {
		if (on_resolve)
			on_resolve();
		return redirected;
	}
	bool opened() {
		++opens;
		if (on_open)
			on_open();
		return success;
	}
	void focusRegained() {
		++focuses;
		if (on_focus)
			on_focus();
	}
};
struct navigation_fixture {
	static constexpr int capacity = 16;
	std::array<UI*, capacity> hierarchy{};
	int depth = 0;
};
static session::State<navigation_fixture> navigation_states;
static navigation_fixture& navigation() {
	return navigation_states.active();
}
static UI* getCurrentUI() {
	return navigation().depth ? navigation().hierarchy[navigation().depth - 1] : nullptr;
}
static session::State<int> redraws;
static void renderUIsForOled() {
	++redraws.active();
}
struct display_fixture {
	bool haveOLED() const { return true; }
};
static display_fixture display_instance;
static auto* display = &display_instance;
enum class TimerName { UI_SPECIFIC };
struct timer_fixture {
	void unsetTimer(TimerName) {}
};
static timer_fixture uiTimerManager;
namespace PadLEDs {
static std::function<void()> on_greyout;
static void reassessGreyout() {
	if (on_greyout)
		on_greyout();
}
} // namespace PadLEDs
#include "ui_open.inc"
} // namespace ui_open_test
using namespace ui_open_test;
TEST_GROUP(UIOpen) {
	UI root, menu, replacement;
	void setup() override {
		session::detail::active = session::Id::Local;
		navigation_states = {};
		redraws = {};
		PadLEDs::on_greyout = {};
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			navigation_states.for_owner(owner).hierarchy[0] = &root;
			navigation_states.for_owner(owner).depth = 1;
		}
	}
	void teardown() override {
		PadLEDs::on_greyout = {};
		session::detail::active = session::Id::Local;
	}
};
TEST(UIOpen, rejected_open_restores_previous_panel_stack) {
	menu.success = false;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		CHECK_FALSE(openUI(&menu));
		LONGS_EQUAL(1, navigation().depth);
		POINTERS_EQUAL(&root, getCurrentUI());
	}
	LONGS_EQUAL(2, root.focuses);
}
TEST(UIOpen, successful_open_routes_to_initiating_panel) {
	session::Scope scope(session::Id::Remote);
	CHECK(openUI(&menu));
	POINTERS_EQUAL(&menu, getCurrentUI());
	LONGS_EQUAL(2, navigation().depth);
	LONGS_EQUAL(1, navigation_states.for_owner(session::Id::Local).depth);
}
TEST(UIOpen, missing_or_full_target_is_rejected) {
	CHECK_FALSE(openUI(nullptr));
	navigation().depth = navigation_fixture::capacity;
	CHECK_FALSE(openUI(&menu));
	LONGS_EQUAL(0, menu.opens);
}

TEST(UIOpen, redirected_null_target_is_rejected_without_stack_change) {
	menu.redirected = nullptr;
	CHECK_FALSE(openUI(&menu));
	LONGS_EQUAL(1, navigation().depth);
	POINTERS_EQUAL(&root, getCurrentUI());
	LONGS_EQUAL(0, menu.opens);
}
TEST(UIOpen, rejected_first_ui_does_not_focus_a_missing_previous_ui) {
	navigation().depth = 0;
	menu.success = false;
	CHECK_FALSE(openUI(&menu));
	LONGS_EQUAL(0, navigation().depth);
	LONGS_EQUAL(0, root.focuses);
}

TEST(UIOpen, nested_open_is_not_rolled_back_by_outer_rejection) {
	menu.success = false;
	menu.on_open = [&] { CHECK(openUI(&replacement)); };
	CHECK_FALSE(openUI(&menu));
	LONGS_EQUAL(3, navigation().depth);
	POINTERS_EQUAL(&replacement, getCurrentUI());
	LONGS_EQUAL(0, root.focuses);
}
TEST(UIOpen, changed_owner_does_not_roll_back_or_redraw_peer_stack) {
	menu.success = false;
	menu.on_open = [] { session::detail::active = session::Id::Remote; };
	CHECK_FALSE(openUI(&menu));
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(1, navigation_states.for_owner(session::Id::Remote).depth);
	LONGS_EQUAL(0, redraws.for_owner(session::Id::Remote));
	LONGS_EQUAL(0, root.focuses);
}
TEST(UIOpen, resolution_stack_change_prevents_insertion) {
	menu.on_resolve = [&] { navigation().hierarchy[0] = &replacement; };
	CHECK_FALSE(openUI(&menu));
	LONGS_EQUAL(1, navigation().depth);
	POINTERS_EQUAL(&replacement, getCurrentUI());
	LONGS_EQUAL(0, menu.opens);
}
TEST(UIOpen, greyout_stack_change_prevents_open_callback) {
	PadLEDs::on_greyout = [&] { navigation().hierarchy[0] = &replacement; };
	CHECK_FALSE(openUI(&menu));
	LONGS_EQUAL(0, menu.opens);
	POINTERS_EQUAL(&replacement, navigation().hierarchy[0]);
}

TEST(UIOpen, rollback_greyout_change_does_not_focus_old_ui) {
	menu.success = false;
	int greyout_calls = 0;
	PadLEDs::on_greyout = [&] {
		if (++greyout_calls == 2)
			navigation().hierarchy[0] = &replacement;
	};
	CHECK_FALSE(openUI(&menu));
	POINTERS_EQUAL(&replacement, getCurrentUI());
	LONGS_EQUAL(0, root.focuses);
	LONGS_EQUAL(0, redraws.active());
}
TEST(UIOpen, focus_callback_owner_change_does_not_redraw_peer) {
	menu.success = false;
	root.on_focus = [] { session::detail::active = session::Id::Remote; };
	CHECK_FALSE(openUI(&menu));
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, redraws.for_owner(session::Id::Remote));
	LONGS_EQUAL(1, navigation().depth);
}
