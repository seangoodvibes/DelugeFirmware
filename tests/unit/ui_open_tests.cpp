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
	bool success = true, main_needed = false, side_needed = false;
	std::function<void()> on_main, on_side;
	int opens = 0, focuses = 0, renders = 0;
	bool renderMainPads(uint32_t = 0, void* = nullptr, void* = nullptr) {
		++renders;
		if (on_main)
			on_main();
		return main_needed;
	}
	bool renderSidebar(uint32_t = 0, void* = nullptr, void* = nullptr) {
		++renders;
		if (on_side)
			on_side();
		return side_needed;
	}
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
static session::State<int> main_sends, side_sends;
static std::function<void()> on_main_send;
static void* image_for_session() {
	return nullptr;
}
static void* occupancy_mask_for_session() {
	return nullptr;
}
static void sendOutMainPadColours() {
	++main_sends.active();
	if (on_main_send)
		on_main_send();
}
static void sendOutSidebarColours() {
	++side_sends.active();
}
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
		PadLEDs::on_main_send = {};
		PadLEDs::main_sends = {};
		PadLEDs::side_sends = {};
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			navigation_states.for_owner(owner).hierarchy[0] = &root;
			navigation_states.for_owner(owner).depth = 1;
		}
	}
	void teardown() override {
		PadLEDs::on_greyout = {};
		PadLEDs::on_main_send = {};
		PadLEDs::main_sends = {};
		PadLEDs::side_sends = {};
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

TEST(UIOpen, valid_close_removes_target_and_descendants_on_initiating_owner) {
	session::Scope scope(session::Id::Remote);
	navigation().hierarchy[1] = &menu;
	navigation().hierarchy[2] = &replacement;
	navigation().depth = 3;
	closeUI(&menu);
	LONGS_EQUAL(1, navigation().depth);
	POINTERS_EQUAL(&root, getCurrentUI());
	LONGS_EQUAL(1, root.focuses);
	LONGS_EQUAL(2, menu.renders);
	LONGS_EQUAL(2, replacement.renders);
	LONGS_EQUAL(1, redraws.active());
	LONGS_EQUAL(0, redraws.for_owner(session::Id::Local));
}

TEST(UIOpen, absent_null_and_root_close_requests_leave_stack_untouched) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	for (auto* target : {static_cast<UI*>(nullptr), &replacement, &root}) {
		closeUI(target);
		LONGS_EQUAL(2, navigation().depth);
		POINTERS_EQUAL(&menu, getCurrentUI());
		LONGS_EQUAL(0, menu.renders);
		LONGS_EQUAL(0, root.focuses);
		LONGS_EQUAL(0, redraws.active());
	}
}
TEST(UIOpen, invalid_depth_close_requests_do_not_access_stack) {
	for (int depth : {-1, 0, 1, navigation_fixture::capacity + 1}) {
		navigation().depth = depth;
		closeUI(&menu);
		LONGS_EQUAL(depth, navigation().depth);
		LONGS_EQUAL(0, root.focuses);
		LONGS_EQUAL(0, redraws.active());
	}
}
TEST(UIOpen, incomplete_stack_is_not_closed) {
	navigation().hierarchy[2] = &menu;
	navigation().depth = 3;
	closeUI(&menu);
	LONGS_EQUAL(3, navigation().depth);
	LONGS_EQUAL(0, menu.renders);
	LONGS_EQUAL(0, root.focuses);
}

TEST(UIOpen, close_query_callback_change_stops_before_pop) {
	for (bool sidebar : {false, true}) {
		navigation().hierarchy[0] = &root;
		navigation().hierarchy[1] = &menu;
		navigation().depth = 2;
		menu.on_main = menu.on_side = {};
		auto invalidate = [&] { navigation().hierarchy[0] = &replacement; };
		if (sidebar)
			menu.on_side = invalidate;
		else
			menu.on_main = invalidate;
		closeUI(&menu);
		LONGS_EQUAL(2, navigation().depth);
		LONGS_EQUAL(0, replacement.focuses);
		LONGS_EQUAL(0, redraws.active());
	}
}
TEST(UIOpen, close_greyout_change_does_not_focus_replaced_ui) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	PadLEDs::on_greyout = [&] { navigation().hierarchy[0] = &replacement; };
	closeUI(&menu);
	POINTERS_EQUAL(&replacement, getCurrentUI());
	LONGS_EQUAL(0, root.focuses);
	LONGS_EQUAL(0, redraws.active());
}
TEST(UIOpen, close_focus_owner_change_does_not_redraw_or_send_peer) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.main_needed = menu.side_needed = true;
	root.on_focus = [] { session::detail::active = session::Id::Remote; };
	closeUI(&menu);
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, redraws.for_owner(session::Id::Remote));
	LONGS_EQUAL(0, PadLEDs::main_sends.for_owner(session::Id::Remote));
	LONGS_EQUAL(0, PadLEDs::side_sends.for_owner(session::Id::Remote));
}
TEST(UIOpen, close_render_callback_change_stops_before_pad_transmission) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.main_needed = menu.side_needed = true;
	root.on_main = [&] { navigation().hierarchy[0] = &replacement; };
	closeUI(&menu);
	LONGS_EQUAL(1, root.renders);
	LONGS_EQUAL(0, PadLEDs::main_sends.active());
	LONGS_EQUAL(0, PadLEDs::side_sends.active());
}
TEST(UIOpen, main_pad_send_callback_cannot_send_sidebar_on_peer) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.main_needed = menu.side_needed = true;
	PadLEDs::on_main_send = [] { session::detail::active = session::Id::Remote; };
	closeUI(&menu);
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(1, PadLEDs::main_sends.for_owner(session::Id::Local));
	LONGS_EQUAL(0, PadLEDs::side_sends.for_owner(session::Id::Remote));
}
TEST(UIOpen, valid_close_sends_both_pad_regions_only_on_initiating_owner) {
	session::Scope scope(session::Id::Remote);
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.main_needed = menu.side_needed = true;
	closeUI(&menu);
	LONGS_EQUAL(1, PadLEDs::main_sends.active());
	LONGS_EQUAL(1, PadLEDs::side_sends.active());
	LONGS_EQUAL(0, PadLEDs::main_sends.for_owner(session::Id::Local));
	LONGS_EQUAL(0, PadLEDs::side_sends.for_owner(session::Id::Local));
}
TEST(UIOpen, close_sidebar_render_change_stops_before_pad_transmission) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.main_needed = menu.side_needed = true;
	root.on_side = [&] { navigation().hierarchy[0] = &replacement; };
	closeUI(&menu);
	LONGS_EQUAL(2, root.renders);
	LONGS_EQUAL(0, PadLEDs::main_sends.active());
	LONGS_EQUAL(0, PadLEDs::side_sends.active());
}
