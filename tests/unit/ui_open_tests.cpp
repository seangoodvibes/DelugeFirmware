#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <algorithm>
#include <array>
#include <functional>
namespace ui_open_test {
namespace session = ::deluge::gui::ui_session;
namespace deluge::gui {
namespace ui_session = ::deluge::gui::ui_session;
}
struct UI {
	bool oledShowsUIUnderneath = false;
	int oled_renders = 0;
	std::function<void()> on_oled;
	void renderOLED(int&) {
		++oled_renders;
		if (on_oled)
			on_oled();
	}
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
	bool oled_dirty = false;
	uint32_t mode = 0;
	uint32_t main_rows_dirty = 0, side_rows_dirty = 0;
};
static session::State<navigation_fixture> navigation_states;
static navigation_fixture& navigation() {
	return navigation_states.active();
}
#define currentUIMode navigation().mode
constexpr uint32_t UI_MODE_HORIZONTAL_SCROLL = 1u << 29;
constexpr uint32_t UI_MODE_HORIZONTAL_ZOOM = 2;
constexpr uint32_t UI_MODE_HOLDING_ARRANGEMENT_ROW = 42; // gui/ui/ui.h
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
static session::State<int> timer_unsets;
struct timer_fixture {
	void unsetTimer(TimerName) { ++timer_unsets.active(); }
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
namespace OLED {
static session::State<int> sends, clears, stops, canvases;
static void clearMainImage() {
	++clears.active();
}
static void stopScrollingAnimation() {
	++stops.active();
}
static void sendMainImage() {
	++sends.active();
}
static int& main_for_session() {
	return canvases.active();
}
} // namespace OLED
namespace deluge::hid::display {
namespace OLED = ::ui_open_test::OLED;
}
#include "ui_open.inc"
} // namespace ui_open_test
using namespace ui_open_test;
TEST_GROUP(UIOpen) {
	UI root, menu, replacement;
	void setup() override {
		session::detail::active = session::Id::Local;
		navigation_states = {};
		redraws = {};
		OLED::sends = {};
		OLED::clears = {};
		OLED::stops = {};
		timer_unsets = {};
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

TEST(UIOpen, replacement_rejection_restores_existing_stack) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	replacement.success = false;
	CHECK_FALSE(changeUIAtLevel(&replacement, 0));
	LONGS_EQUAL(2, navigation().depth);
	POINTERS_EQUAL(&root, navigation().hierarchy[0]);
	POINTERS_EQUAL(&menu, getCurrentUI());
	LONGS_EQUAL(1, menu.focuses);
}
TEST(UIOpen, replacement_callback_stack_change_is_not_rolled_back) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	replacement.success = false;
	replacement.on_open = [&] { CHECK(openUI(&menu)); };
	CHECK_FALSE(changeUIAtLevel(&replacement, 0));
	POINTERS_EQUAL(&replacement, navigation().hierarchy[0]);
	POINTERS_EQUAL(&menu, getCurrentUI());
	LONGS_EQUAL(0, menu.focuses);
}
TEST(UIOpen, replacement_owner_change_does_not_restore_into_peer_stack) {
	session::Scope scope(session::Id::Remote);
	replacement.success = false;
	replacement.on_open = [] { session::detail::active = session::Id::Local; };
	CHECK_FALSE(changeUIAtLevel(&replacement, 0));
	CHECK(session::current() == session::Id::Remote);
	POINTERS_EQUAL(&root, navigation_states.for_owner(session::Id::Local).hierarchy[0]);
	LONGS_EQUAL(0, root.focuses);
}
TEST(UIOpen, sideways_resolution_change_prevents_replacement_and_redraw) {
	menu.on_resolve = [&] { navigation().hierarchy[0] = &replacement; };
	CHECK_FALSE(changeUISideways(&menu));
	POINTERS_EQUAL(&replacement, getCurrentUI());
	LONGS_EQUAL(0, menu.opens);
	LONGS_EQUAL(0, redraws.active());
}
TEST(UIOpen, sideways_nested_open_is_not_redrawn_by_outer_operation) {
	menu.on_open = [&] { CHECK(openUI(&replacement)); };
	CHECK_FALSE(changeUISideways(&menu));
	POINTERS_EQUAL(&replacement, getCurrentUI());
	LONGS_EQUAL(1, redraws.active());
}

TEST(UIOpen, invalid_replacement_targets_and_levels_leave_stack_untouched) {
	CHECK_FALSE(changeUIAtLevel(nullptr, 0));
	CHECK_FALSE(changeUISideways(nullptr));
	for (int level : {-1, 1, navigation_fixture::capacity})
		CHECK_FALSE(changeUIAtLevel(&menu, level));
	menu.redirected = nullptr;
	CHECK_FALSE(changeUISideways(&menu));
	POINTERS_EQUAL(&root, getCurrentUI());
	LONGS_EQUAL(0, menu.opens);
	LONGS_EQUAL(0, redraws.active());
}
TEST(UIOpen, sideways_success_and_rejection_preserve_other_panel) {
	session::Scope scope(session::Id::Remote);
	CHECK(changeUISideways(&menu));
	POINTERS_EQUAL(&menu, getCurrentUI());
	replacement.success = false;
	CHECK_FALSE(changeUISideways(&replacement));
	POINTERS_EQUAL(&menu, getCurrentUI());
	LONGS_EQUAL(1, menu.focuses);
	LONGS_EQUAL(2, redraws.active());
	POINTERS_EQUAL(&root, navigation_states.for_owner(session::Id::Local).hierarchy[0]);
	LONGS_EQUAL(0, redraws.for_owner(session::Id::Local));
}
TEST(UIOpen, malformed_replacement_stack_is_rejected_without_redraw) {
	for (int depth : {-1, 0, navigation_fixture::capacity + 1, 2}) {
		navigation().depth = depth;
		CHECK_FALSE(changeUIAtLevel(&menu, 0));
		CHECK_FALSE(changeUISideways(&menu));
		LONGS_EQUAL(depth, navigation().depth);
		LONGS_EQUAL(0, menu.opens);
		LONGS_EQUAL(0, redraws.active());
	}
}
TEST(UIOpen, replacement_greyout_changes_stop_opening_and_rollback_focus) {
	for (int phase : {1, 2}) {
		navigation().depth = 1;
		navigation().hierarchy[0] = &root;
		menu.opens = 0;
		menu.success = false;
		int calls = 0;
		PadLEDs::on_greyout = [&] {
			if (++calls == phase)
				navigation().hierarchy[0] = &replacement;
		};
		CHECK_FALSE(changeUIAtLevel(&menu, 0));
		POINTERS_EQUAL(&replacement, getCurrentUI());
		LONGS_EQUAL(phase == 1 ? 0 : 1, menu.opens);
		LONGS_EQUAL(0, root.focuses);
	}
}

TEST(UIOpen, root_resolution_changes_do_not_overwrite_newer_navigation) {
	for (bool low_level : {false, true}) {
		navigation().hierarchy[0] = &root;
		menu.on_resolve = [&] { navigation().hierarchy[0] = &replacement; };
		if (low_level)
			setRootUILowLevel(&menu);
		else
			changeRootUI(&menu);
		POINTERS_EQUAL(&replacement, getCurrentUI());
		LONGS_EQUAL(0, menu.opens);
		LONGS_EQUAL(0, redraws.active());
	}
}
TEST(UIOpen, root_greyout_change_does_not_open_obsolete_target) {
	PadLEDs::on_greyout = [&] { navigation().hierarchy[0] = &replacement; };
	changeRootUI(&menu);
	POINTERS_EQUAL(&replacement, getCurrentUI());
	LONGS_EQUAL(0, menu.opens);
	LONGS_EQUAL(0, redraws.active());
}
TEST(UIOpen, root_open_callback_owner_change_does_not_redraw_peer) {
	menu.on_open = [] { session::detail::active = session::Id::Remote; };
	changeRootUI(&menu);
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, redraws.for_owner(session::Id::Remote));
	POINTERS_EQUAL(&root, navigation_states.for_owner(session::Id::Remote).hierarchy[0]);
}
TEST(UIOpen, normal_root_change_preserves_arrangement_hold_timer) {
	session::Scope scope(session::Id::Remote);
	currentUIMode = UI_MODE_HOLDING_ARRANGEMENT_ROW;
	changeRootUI(&menu);
	POINTERS_EQUAL(&menu, getCurrentUI());
	LONGS_EQUAL(1, menu.opens);
	LONGS_EQUAL(0, timer_unsets.active());
	LONGS_EQUAL(1, redraws.active());
	currentUIMode = 0;
	changeRootUI(&replacement);
	LONGS_EQUAL(1, timer_unsets.active());
	POINTERS_EQUAL(&root, navigation_states.for_owner(session::Id::Local).hierarchy[0]);
}
TEST(UIOpen, low_level_root_installation_does_not_open_or_render) {
	setRootUILowLevel(&menu);
	POINTERS_EQUAL(&menu, getCurrentUI());
	LONGS_EQUAL(1, navigation().depth);
	LONGS_EQUAL(0, menu.opens);
	LONGS_EQUAL(0, timer_unsets.active());
	LONGS_EQUAL(0, redraws.active());
}

TEST(UIOpen, missing_root_targets_and_invalid_depth_are_rejected) {
	for (bool low_level : {false, true}) {
		auto install = [&](UI* target) {
			if (low_level)
				setRootUILowLevel(target);
			else
				changeRootUI(target);
		};
		install(nullptr);
		menu.redirected = nullptr;
		install(&menu);
		POINTERS_EQUAL(&root, getCurrentUI());
		menu.redirected = &menu;
		for (int depth : {-1, navigation_fixture::capacity + 1}) {
			navigation().depth = depth;
			install(&menu);
			LONGS_EQUAL(depth, navigation().depth);
		}
		navigation().depth = 1;
		LONGS_EQUAL(0, menu.opens);
		LONGS_EQUAL(0, redraws.active());
	}
}
TEST(UIOpen, low_level_greyout_owner_change_restores_caller) {
	PadLEDs::on_greyout = [] { session::detail::active = session::Id::Remote; };
	setRootUILowLevel(&menu);
	CHECK(session::current() == session::Id::Local);
	POINTERS_EQUAL(&menu, getCurrentUI());
	POINTERS_EQUAL(&root, navigation_states.for_owner(session::Id::Remote).hierarchy[0]);
}

TEST(UIOpen, rendering_request_stays_with_initiating_panel) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.on_main = [] { session::detail::active = session::Id::Remote; };
	uiNeedsRendering(&root, 1, 2);
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, navigation_states.for_owner(session::Id::Remote).main_rows_dirty);
	LONGS_EQUAL(1, menu.renders);
}
TEST(UIOpen, rendering_request_stops_when_visibility_callback_changes_stack) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.on_main = [&] { navigation().hierarchy[0] = &replacement; };
	uiNeedsRendering(&root, 1, 2);
	LONGS_EQUAL(1, menu.renders);
	LONGS_EQUAL(0, navigation().main_rows_dirty);
}
TEST(UIOpen, rendering_request_marks_only_visible_regions) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.main_needed = true;
	uiNeedsRendering(&root, 1, 2);
	LONGS_EQUAL(0, navigation().main_rows_dirty);
	LONGS_EQUAL(2, navigation().side_rows_dirty);
}

TEST(UIOpen, rendering_request_rejects_invalid_targets_and_stacks) {
	uiNeedsRendering(nullptr, 1, 2);
	LONGS_EQUAL(0, root.renders);
	for (int depth : {-1, 0, navigation_fixture::capacity + 1}) {
		navigation().depth = depth;
		uiNeedsRendering(&root, 1, 2);
		LONGS_EQUAL(0, navigation().main_rows_dirty);
	}
	navigation().depth = 2;
	navigation().hierarchy[1] = nullptr;
	uiNeedsRendering(&root, 1, 2);
	LONGS_EQUAL(0, root.renders);
}
TEST(UIOpen, sidebar_visibility_change_stops_render_request) {
	navigation().hierarchy[1] = &menu;
	navigation().depth = 2;
	menu.on_side = [] { session::detail::active = session::Id::Remote; };
	uiNeedsRendering(&root, 1, 2);
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, navigation_states.for_owner(session::Id::Remote).side_rows_dirty);
	LONGS_EQUAL(0, navigation().side_rows_dirty);
}

TEST(UIOpen, grid_render_stops_before_sending_after_owner_change) {
	navigation().main_rows_dirty = 1;
	navigation().side_rows_dirty = 2;
	root.main_needed = root.side_needed = true;
	root.on_main = [] { session::detail::active = session::Id::Remote; };
	doAnyPendingGridRendering();
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, PadLEDs::main_sends.for_owner(session::Id::Remote));
	LONGS_EQUAL(1, root.renders);
	LONGS_EQUAL(1, navigation().main_rows_dirty);
	LONGS_EQUAL(2, navigation().side_rows_dirty);
}
TEST(UIOpen, grid_render_retries_after_stack_changes) {
	navigation().main_rows_dirty = 1;
	root.main_needed = true;
	root.on_main = [&] { navigation().hierarchy[0] = &replacement; };
	doAnyPendingGridRendering();
	LONGS_EQUAL(0, PadLEDs::main_sends.active());
	LONGS_EQUAL(1, navigation().main_rows_dirty);
	replacement.main_needed = true;
	doAnyPendingGridRendering();
	LONGS_EQUAL(1, PadLEDs::main_sends.active());
	LONGS_EQUAL(0, navigation().main_rows_dirty);
}
TEST(UIOpen, grid_render_preserves_new_requests_and_normal_sends) {
	navigation().main_rows_dirty = 1;
	navigation().side_rows_dirty = 2;
	root.main_needed = root.side_needed = true;
	root.on_main = [] { navigation().main_rows_dirty = 4; };
	doAnyPendingGridRendering();
	LONGS_EQUAL(4, navigation().main_rows_dirty);
	LONGS_EQUAL(0, PadLEDs::main_sends.active());
	LONGS_EQUAL(1, PadLEDs::side_sends.active());
	root.on_main = {};
	doAnyPendingGridRendering();
	LONGS_EQUAL(1, PadLEDs::main_sends.active());
}

TEST(UIOpen, grid_render_stops_after_main_send_changes_context) {
	navigation().main_rows_dirty = 1;
	navigation().side_rows_dirty = 2;
	root.main_needed = root.side_needed = true;
	PadLEDs::on_main_send = [] { session::detail::active = session::Id::Remote; };
	doAnyPendingGridRendering();
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(1, root.renders);
	LONGS_EQUAL(0, PadLEDs::side_sends.for_owner(session::Id::Remote));
	LONGS_EQUAL(2, navigation().side_rows_dirty);
}
TEST(UIOpen, grid_render_defers_invalid_stacks_and_animation_modes) {
	for (int depth : {-1, 0, navigation_fixture::capacity + 1, 2}) {
		navigation().depth = depth;
		navigation().main_rows_dirty = 1;
		doAnyPendingGridRendering();
		LONGS_EQUAL(1, navigation().main_rows_dirty);
	}
	navigation().depth = 1;
	for (auto mode : {UI_MODE_HORIZONTAL_SCROLL, UI_MODE_HORIZONTAL_ZOOM}) {
		navigation().mode = mode;
		doAnyPendingGridRendering();
		LONGS_EQUAL(1, navigation().main_rows_dirty);
	}
	LONGS_EQUAL(0, root.renders);
}
TEST(UIOpen, grid_sidebar_change_preserves_callback_requests) {
	navigation().side_rows_dirty = 2;
	root.side_needed = true;
	root.on_side = [&] {
		navigation().side_rows_dirty = 4;
		navigation().hierarchy[0] = &replacement;
	};
	doAnyPendingGridRendering();
	LONGS_EQUAL(6, navigation().side_rows_dirty);
	LONGS_EQUAL(0, PadLEDs::side_sends.active());
}

TEST(UIOpen, oled_render_preserves_request_queued_by_callback) {
	navigation().oled_dirty = true;
	root.on_oled = [] { navigation().oled_dirty = true; };
	doAnyPendingOLEDRendering();
	CHECK(navigation().oled_dirty);
}
TEST(UIOpen, oled_render_does_not_send_into_changed_owner) {
	navigation().oled_dirty = true;
	root.on_oled = [] { session::detail::active = session::Id::Remote; };
	doAnyPendingOLEDRendering();
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, OLED::sends.for_owner(session::Id::Remote));
	CHECK(navigation().oled_dirty);
}
TEST(UIOpen, oled_render_stops_after_stack_change_and_retries) {
	navigation().oled_dirty = true;
	navigation().depth = 2;
	navigation().hierarchy[1] = &menu;
	menu.oledShowsUIUnderneath = true;
	root.on_oled = [&] { navigation().hierarchy[1] = &replacement; };
	doAnyPendingOLEDRendering();
	LONGS_EQUAL(0, replacement.oled_renders);
	LONGS_EQUAL(0, OLED::sends.active());
	CHECK(navigation().oled_dirty);
	root.on_oled = {};
	doAnyPendingOLEDRendering();
	LONGS_EQUAL(1, replacement.oled_renders);
	LONGS_EQUAL(1, OLED::sends.active());
	CHECK_FALSE(navigation().oled_dirty);
}

TEST(UIOpen, oled_render_handles_layers_and_clean_frame_sends) {
	navigation().depth = 2;
	navigation().hierarchy[1] = &menu;
	navigation().oled_dirty = true;
	menu.oledShowsUIUnderneath = true;
	doAnyPendingOLEDRendering();
	LONGS_EQUAL(1, root.oled_renders);
	LONGS_EQUAL(1, menu.oled_renders);
	LONGS_EQUAL(1, OLED::clears.active());
	CHECK_FALSE(navigation().oled_dirty);
	doAnyPendingOLEDRendering();
	LONGS_EQUAL(2, OLED::sends.active());
	LONGS_EQUAL(1, menu.oled_renders);
	navigation().oled_dirty = true;
	menu.oledShowsUIUnderneath = false;
	doAnyPendingOLEDRendering();
	LONGS_EQUAL(1, root.oled_renders);
	LONGS_EQUAL(2, menu.oled_renders);
}
TEST(UIOpen, oled_render_defers_invalid_stacks_without_consuming_request) {
	for (int depth : {-1, 0, navigation_fixture::capacity + 1, 2}) {
		navigation().depth = depth;
		navigation().oled_dirty = true;
		doAnyPendingOLEDRendering();
		CHECK(navigation().oled_dirty);
	}
	LONGS_EQUAL(0, OLED::clears.active());
	LONGS_EQUAL(0, OLED::sends.active());
}

TEST(UIOpen, root_swap_does_not_overwrite_peer_after_target_resolution) {
	menu.on_resolve = [] { session::detail::active = session::Id::Remote; };
	swapOutRootUILowLevel(&menu);
	CHECK(session::current() == session::Id::Local);
	POINTERS_EQUAL(&root, navigation_states.for_owner(session::Id::Remote).hierarchy[0]);
	POINTERS_EQUAL(&root, navigation().hierarchy[0]);
}
TEST(UIOpen, root_swap_preserves_navigation_changed_by_target_resolution) {
	menu.on_resolve = [&] { navigation().hierarchy[0] = &replacement; };
	swapOutRootUILowLevel(&menu);
	POINTERS_EQUAL(&replacement, navigation().hierarchy[0]);
}
TEST(UIOpen, root_swap_keeps_overlays_and_avoids_opening_callbacks) {
	session::Scope scope(session::Id::Remote);
	navigation().depth = 2;
	navigation().hierarchy[1] = &menu;
	swapOutRootUILowLevel(&replacement);
	POINTERS_EQUAL(&replacement, navigation().hierarchy[0]);
	POINTERS_EQUAL(&menu, navigation().hierarchy[1]);
	POINTERS_EQUAL(&root, navigation_states.for_owner(session::Id::Local).hierarchy[0]);
	LONGS_EQUAL(2, navigation().depth);
	LONGS_EQUAL(0, replacement.opens);
	LONGS_EQUAL(0, redraws.active());
}

TEST(UIOpen, root_swap_rejects_missing_targets_and_invalid_stacks) {
	swapOutRootUILowLevel(nullptr);
	menu.redirected = nullptr;
	swapOutRootUILowLevel(&menu);
	POINTERS_EQUAL(&root, navigation().hierarchy[0]);
	menu.redirected = &menu;
	int resolutions = 0;
	menu.on_resolve = [&] { ++resolutions; };
	for (int depth : {-1, 0, navigation_fixture::capacity + 1, 2}) {
		navigation().depth = depth;
		swapOutRootUILowLevel(&menu);
		POINTERS_EQUAL(&root, navigation().hierarchy[0]);
	}
	LONGS_EQUAL(0, resolutions);
}
TEST(UIOpen, root_swap_preserves_depth_change_during_resolution) {
	menu.on_resolve = [] { navigation().depth = 0; };
	swapOutRootUILowLevel(&menu);
	LONGS_EQUAL(0, navigation().depth);
	POINTERS_EQUAL(&root, navigation().hierarchy[0]);
}
