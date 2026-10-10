#include "CppUTest/TestHarness.h"
#include "gui/ui/graphics_routing.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <algorithm>
#include <array>
#include <functional>
#include <new>
namespace ui_open_test {
namespace deluge {
namespace lifetime = ::deluge::lifetime;
}
enum class ClipType { INSTRUMENT, AUDIO };
struct Clip {
	ClipType type = ClipType::INSTRUMENT;
	bool automation = false;
	bool on_automation_clip_view_for_session() { return automation; }
};
struct InstrumentClip : Clip {
	bool keyboard = false;
	bool on_keyboard_screen_for_session() { return keyboard; }
};
struct Song {
	Clip* clip = nullptr;
	bool inClipMinderViewOnLoad = false;
	int32_t last_instance = -1;
	Clip* getCurrentClip() { return clip; }
	int32_t last_clip_instance_entered_start_pos_for_session() { return last_instance; }

	::deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return ::deluge::lifetime::lifetime_watch(lifetime); }
};
static Song song;
static Song* currentSong = &song;
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
	int refreshes = 0;
	std::function<void()> on_refresh;
	void refresh_shared_model() {
		++refreshes;
		if (on_refresh)
			on_refresh();
	}
	bool greyout_used = false;
	std::function<void()> on_greyout_query;
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) {
		*cols = 3;
		*rows = 4;
		if (on_greyout_query)
			on_greyout_query();
		return greyout_used;
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
	bool rendering = false;
	session::SharedModelRefresh shared_model_refresh;
	session::StructuralRefresh structural_refresh;
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
static std::function<void()> on_main_send, on_side_send;
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
	if (on_side_send)
		on_side_send();
}
static std::function<void()> on_greyout;
static void reassessGreyout() {
	if (on_greyout)
		on_greyout();
}
} // namespace PadLEDs
namespace OLED {
static session::State<int> sends, clears, stops, canvases;
static std::function<void()> on_clear, on_stop, on_send;
static void clearMainImage() {
	++clears.active();
	if (on_clear)
		on_clear();
}
static void stopScrollingAnimation() {
	++stops.active();
	if (on_stop)
		on_stop();
}
static void sendMainImage() {
	++sends.active();
	if (on_send)
		on_send();
}
static int& main_for_session() {
	return canvases.active();
}
} // namespace OLED
namespace deluge::hid::display {
namespace OLED = ::ui_open_test::OLED;
}
static bool sdRoutineLock = false, currentlyAccessingCard = false, client_mode = false;
static int uart_space = 1000, uart_queries = 0;
constexpr int UART_ITEM_PIC_PADS = 0, kNumBytesInMainPadRedraw = 10, kNumBytesInSidebarRedraw = 5;
static int uartGetTxBufferSpace(int) {
	++uart_queries;
	return uart_space;
}
namespace hid = deluge::hid;
namespace deluge::hid::mirror {
static bool is_client() {
	return client_mode;
}
} // namespace deluge::hid::mirror
namespace deluge::modulation::automation {
static uint64_t parameter_revision = 0;
}
static UI overview_ui, arranger_ui;
static UI instrument_ui, automation_ui, keyboard_ui, audio_ui;
static std::function<void()> on_view_access;
static UI& instrument_clip_view_for_session() {
	if (on_view_access)
		on_view_access();
	return instrument_ui;
}
static UI& automation_view_for_session() {
	if (on_view_access)
		on_view_access();
	return automation_ui;
}
static UI& keyboard_screen_for_session() {
	if (on_view_access)
		on_view_access();
	return keyboard_ui;
}
static UI& audio_clip_view_for_session() {
	if (on_view_access)
		on_view_access();
	return audio_ui;
}

static UI& session_view_for_session() {
	return overview_ui;
}
static UI& arranger_view_for_session() {
	return arranger_ui;
}
void setRootUILowLevel(UI*);
void uiNeedsRendering(UI*, uint32_t = 0xffffffff, uint32_t = 0xffffffff);
#include "loaded_song_ui.inc"
#include "ui_open.inc"
namespace greyout_effects {
static session::State<uint32_t> cols, rows, start_time;
static session::State<int> direction, main_requests, side_requests, timer_sets, amounts;
static uint32_t& greyout_cols_for_session() {
	return cols.active();
}
static uint32_t& greyout_rows_for_session() {
	return rows.active();
}
static uint32_t& greyout_change_start_time_for_session() {
	return start_time.active();
}
static int& greyout_change_direction_for_session() {
	return direction.active();
}
static void setGreyoutAmount(int value) {
	amounts.active() = value;
}
static void sendOutMainPadColoursSoon() {
	++main_requests.active();
}
static void sendOutSidebarColoursSoon() {
	++side_requests.active();
}
namespace AudioEngine {
constexpr uint32_t audioSampleTimer = 99;
}
enum class TimerName { MATRIX_DRIVER };
constexpr int UI_MS_PER_REFRESH = 15;
struct timer_fixture {
	void setTimer(TimerName, int) { ++timer_sets.active(); }
};
static timer_fixture uiTimerManager;
#include "greyout_reassess.inc"
} // namespace greyout_effects
} // namespace ui_open_test
using namespace ui_open_test;
TEST_GROUP(UIOpen) {
	ui_open_test::UI root, menu, replacement;
	void setup() override {
		session::detail::active = session::Id::Local;
		navigation_states = {};
		currentSong = &song;
		song.clip = nullptr;
		song.inClipMinderViewOnLoad = false;
		song.last_instance = -1;
		instrument_ui = {};
		automation_ui = {};
		keyboard_ui = {};
		audio_ui = {};
		on_view_access = {};
		greyout_effects::cols = {};
		greyout_effects::rows = {};
		greyout_effects::direction = {};
		greyout_effects::main_requests = {};
		greyout_effects::side_requests = {};
		greyout_effects::timer_sets = {};
		greyout_effects::amounts = {};
		sdRoutineLock = currentlyAccessingCard = client_mode = false;
		uart_space = 1000;
		uart_queries = 0;
		ui_open_test::deluge::modulation::automation::parameter_revision = 0;
		overview_ui = {};
		arranger_ui = {};
		for (auto* target : {&overview_ui, &arranger_ui, &instrument_ui, &automation_ui, &keyboard_ui, &audio_ui})
			target->redirected = target;
		redraws = {};
		OLED::sends = {};
		OLED::clears = {};
		OLED::stops = {};
		timer_unsets = {};
		PadLEDs::on_greyout = {};
		PadLEDs::on_main_send = {};
		PadLEDs::on_side_send = {};
		OLED::on_clear = {};
		OLED::on_stop = {};
		OLED::on_send = {};
		PadLEDs::main_sends = {};
		PadLEDs::side_sends = {};
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			navigation_states.for_owner(owner).hierarchy[0] = &root;
			navigation_states.for_owner(owner).depth = 1;
		}
	}
	void teardown() override {
		on_view_access = {};
		song.clip = nullptr;
		currentSong = &song;
		PadLEDs::on_greyout = {};
		PadLEDs::on_main_send = {};
		PadLEDs::on_side_send = {};
		OLED::on_clear = {};
		OLED::on_stop = {};
		OLED::on_send = {};
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
	for (auto* target : {static_cast<ui_open_test::UI*>(nullptr), &replacement, &root}) {
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
		auto install = [&](ui_open_test::UI* target) {
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

TEST(UIOpen, render_pass_clears_initiating_flag_after_refresh_changes_owner) {
	navigation().shared_model_refresh.request();
	root.on_refresh = [] { session::detail::active = session::Id::Remote; };
	doAnyPendingUIRendering();
	CHECK(session::current() == session::Id::Local);
	CHECK_FALSE(navigation().rendering);
	LONGS_EQUAL(0, OLED::sends.for_owner(session::Id::Remote));
}
TEST(UIOpen, render_pass_stops_after_refresh_changes_stack) {
	navigation().shared_model_refresh.request();
	navigation().oled_dirty = true;
	root.on_refresh = [&] { navigation().hierarchy[0] = &replacement; };
	doAnyPendingUIRendering();
	LONGS_EQUAL(0, replacement.oled_renders);
	CHECK_FALSE(navigation().rendering);
	CHECK(navigation().oled_dirty);
}
TEST(UIOpen, render_pass_reentrancy_is_suppressed) {
	navigation().shared_model_refresh.request();
	root.on_refresh = [] { doAnyPendingUIRendering(); };
	doAnyPendingUIRendering();
	LONGS_EQUAL(1, root.refreshes);
	LONGS_EQUAL(1, OLED::sends.active());
	CHECK_FALSE(navigation().rendering);
}

TEST(UIOpen, render_pass_restores_flag_when_refresh_throws) {
	navigation().shared_model_refresh.request();
	root.on_refresh = [] { throw 7; };
	bool caught = false;
	try {
		doAnyPendingUIRendering();
	} catch (int error) {
		caught = error == 7;
	}
	CHECK(caught);
	CHECK_FALSE(navigation().rendering);
	CHECK(session::current() == session::Id::Local);
}
TEST(UIOpen, render_pass_retries_refresh_for_changed_stack) {
	navigation().shared_model_refresh.request();
	root.on_refresh = [&] { navigation().hierarchy[0] = &replacement; };
	doAnyPendingUIRendering();
	doAnyPendingUIRendering();
	LONGS_EQUAL(1, replacement.refreshes);
	CHECK_FALSE(navigation().rendering);
}
TEST(UIOpen, render_pass_defers_storage_refresh_and_local_output_backpressure) {
	navigation().shared_model_refresh.request();
	uart_space = 0;
	doAnyPendingUIRendering();
	LONGS_EQUAL(0, root.refreshes);
	CHECK_FALSE(navigation().rendering);
	uart_space = 1000;
	sdRoutineLock = true;
	doAnyPendingUIRendering();
	LONGS_EQUAL(0, root.refreshes);
	sdRoutineLock = false;
	doAnyPendingUIRendering();
	LONGS_EQUAL(1, root.refreshes);
}
TEST(UIOpen, render_pass_remote_bypasses_hardware_capacity_and_client_defers) {
	session::Scope scope(session::Id::Remote);
	uart_space = 0;
	client_mode = true;
	doAnyPendingUIRendering();
	LONGS_EQUAL(0, OLED::sends.active());
	client_mode = false;
	doAnyPendingUIRendering();
	LONGS_EQUAL(0, uart_queries);
	LONGS_EQUAL(1, OLED::sends.active());
	CHECK_FALSE(navigation().rendering);
}
TEST(UIOpen, render_pass_skips_oled_after_grid_changes_stack) {
	navigation().main_rows_dirty = 1;
	navigation().oled_dirty = true;
	root.on_main = [&] { navigation().hierarchy[0] = &replacement; };
	doAnyPendingUIRendering();
	LONGS_EQUAL(0, replacement.oled_renders);
	CHECK_FALSE(navigation().rendering);
}

TEST(UIOpen, greyout_query_cannot_update_peer_after_owner_change) {
	root.greyout_used = true;
	root.on_greyout_query = [] { session::detail::active = session::Id::Remote; };
	greyout_effects::reassessGreyout(false);
	CHECK(session::current() == session::Id::Local);
	LONGS_EQUAL(0, greyout_effects::cols.for_owner(session::Id::Remote));
	LONGS_EQUAL(0, greyout_effects::timer_sets.for_owner(session::Id::Remote));
	LONGS_EQUAL(0, greyout_effects::cols.active());
}
TEST(UIOpen, greyout_query_cannot_apply_results_from_replaced_stack) {
	root.greyout_used = true;
	root.on_greyout_query = [&] { navigation().hierarchy[0] = &replacement; };
	greyout_effects::reassessGreyout(false);
	LONGS_EQUAL(0, greyout_effects::cols.active());
	LONGS_EQUAL(0, greyout_effects::timer_sets.active());
}
TEST(UIOpen, greyout_query_normal_fade_and_instant_updates) {
	root.greyout_used = true;
	greyout_effects::reassessGreyout(false);
	LONGS_EQUAL(3, greyout_effects::cols.active());
	LONGS_EQUAL(4, greyout_effects::rows.active());
	LONGS_EQUAL(1, greyout_effects::timer_sets.active());
	LONGS_EQUAL(1, greyout_effects::direction.active());
	greyout_effects::cols.active() = 1;
	greyout_effects::reassessGreyout(true);
	LONGS_EQUAL(1, greyout_effects::main_requests.active());
	LONGS_EQUAL(1, greyout_effects::side_requests.active());
}

TEST(UIOpen, greyout_query_defers_invalid_stacks) {
	for (int depth : {-1, navigation_fixture::capacity + 1, 2}) {
		navigation().depth = depth;
		greyout_effects::reassessGreyout(false);
		LONGS_EQUAL(0, greyout_effects::timer_sets.active());
	}
}
TEST(UIOpen, greyout_query_uses_highest_covering_ui_and_handles_empty_stack) {
	navigation().depth = 2;
	navigation().hierarchy[1] = &menu;
	root.greyout_used = true;
	greyout_effects::reassessGreyout(false);
	LONGS_EQUAL(3, greyout_effects::cols.active());
	LONGS_EQUAL(1, greyout_effects::timer_sets.active());
	greyout_effects::reassessGreyout(false);
	LONGS_EQUAL(1, greyout_effects::timer_sets.active());
	navigation().depth = 0;
	greyout_effects::reassessGreyout(false);
	LONGS_EQUAL(-1, greyout_effects::direction.active());
	LONGS_EQUAL(2, greyout_effects::timer_sets.active());
}

TEST(UIOpen, oled_setup_callbacks_cancel_on_either_panel_without_peer_output) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		const auto peer = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		for (int phase = 0; phase < 2; ++phase) {
			session::Scope scope(owner);
			navigation().oled_dirty = true;
			OLED::on_clear = {};
			OLED::on_stop = {};
			auto change_owner = [peer] { session::detail::active = peer; };
			if (phase == 0)
				OLED::on_clear = change_owner;
			else
				OLED::on_stop = change_owner;
			doAnyPendingOLEDRendering();
			CHECK(session::current() == owner);
			CHECK(navigation().oled_dirty);
			LONGS_EQUAL(0, root.oled_renders);
			LONGS_EQUAL(0, OLED::sends.for_owner(peer));
		}
	}
}
TEST(UIOpen, oled_send_restores_owner_and_render_pass_flag) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		const auto peer = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		session::Scope scope(owner);
		OLED::on_send = [peer] { session::detail::active = peer; };
		doAnyPendingUIRendering();
		CHECK(session::current() == owner);
		CHECK_FALSE(navigation().rendering);
		LONGS_EQUAL(1, OLED::sends.for_owner(owner));
	}
}
TEST(UIOpen, sidebar_send_change_requeues_only_on_initiating_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		const auto peer = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		session::Scope scope(owner);
		navigation_states.for_owner(peer).side_rows_dirty = 8;
		navigation().side_rows_dirty = 2;
		root.side_needed = true;
		PadLEDs::on_side_send = [peer] { session::detail::active = peer; };
		doAnyPendingGridRendering();
		CHECK(session::current() == owner);
		LONGS_EQUAL(2, navigation().side_rows_dirty);
		LONGS_EQUAL(8, navigation_states.for_owner(peer).side_rows_dirty);
		LONGS_EQUAL(1, PadLEDs::side_sends.for_owner(owner));
	}
}
TEST(UIOpen, greyout_cancel_preserves_existing_masks_and_pending_fade) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		const auto peer = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		session::Scope scope(owner);
		greyout_effects::cols.active() = 9;
		greyout_effects::rows.active() = 10;
		greyout_effects::direction.active() = -1;
		root.greyout_used = true;
		root.on_greyout_query = [peer] { session::detail::active = peer; };
		greyout_effects::reassessGreyout(true);
		CHECK(session::current() == owner);
		LONGS_EQUAL(9, greyout_effects::cols.active());
		LONGS_EQUAL(10, greyout_effects::rows.active());
		LONGS_EQUAL(-1, greyout_effects::direction.active());
		LONGS_EQUAL(0, greyout_effects::main_requests.active());
		LONGS_EQUAL(0, greyout_effects::side_requests.active());
	}
}
TEST(UIOpen, layered_grid_render_keeps_occluded_regions_separate) {
	session::Scope scope(session::Id::Remote);
	navigation().depth = 2;
	navigation().hierarchy[1] = &menu;
	navigation().main_rows_dirty = 1;
	navigation().side_rows_dirty = 2;
	menu.main_needed = true;
	root.side_needed = true;
	doAnyPendingGridRendering();
	LONGS_EQUAL(2, menu.renders);
	LONGS_EQUAL(1, root.renders);
	LONGS_EQUAL(1, PadLEDs::main_sends.active());
	LONGS_EQUAL(1, PadLEDs::side_sends.active());
	LONGS_EQUAL(0, PadLEDs::main_sends.for_owner(session::Id::Local));
	LONGS_EQUAL(0, PadLEDs::side_sends.for_owner(session::Id::Local));
}

TEST(UIOpen, song_reuse_during_resolution_does_not_publish_new_navigation) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int operation = 0; operation < 5; ++operation) {
			navigation().depth = 1;
			navigation().hierarchy[0] = &root;
			menu.on_resolve = [] {
				song.~Song();
				new (&song) Song;
			};
			if (operation == 0)
				CHECK_FALSE(openUI(&menu));
			else if (operation == 1)
				CHECK_FALSE(changeUISideways(&menu));
			else if (operation == 2)
				changeRootUI(&menu);
			else if (operation == 3)
				setRootUILowLevel(&menu);
			else
				swapOutRootUILowLevel(&menu);
			LONGS_EQUAL(1, navigation().depth);
			POINTERS_EQUAL(&root, navigation().hierarchy[0]);
			LONGS_EQUAL(0, menu.opens);
		}
	}
}

TEST(UIOpen, song_reuse_during_failed_open_does_not_restore_or_focus_old_ui) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (bool replace : {false, true}) {
			navigation().depth = 1;
			navigation().hierarchy[0] = &root;
			menu.success = false;
			menu.on_open = [] {
				song.~Song();
				new (&song) Song;
			};
			CHECK_FALSE(replace ? changeUIAtLevel(&menu, 0) : openUI(&menu));
			LONGS_EQUAL(replace ? 1 : 2, navigation().depth);
			POINTERS_EQUAL(&menu, getCurrentUI());
			LONGS_EQUAL(0, root.focuses);
			LONGS_EQUAL(0, redraws.active());
		}
	}
}

TEST(UIOpen, song_reuse_during_greyout_prevents_opening_callback) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int operation = 0; operation < 3; ++operation) {
			navigation().depth = 1;
			navigation().hierarchy[0] = &root;
			PadLEDs::on_greyout = [] {
				song.~Song();
				new (&song) Song;
			};
			if (operation == 0)
				CHECK_FALSE(openUI(&menu));
			else if (operation == 1)
				CHECK_FALSE(changeUIAtLevel(&menu, 0));
			else
				changeRootUI(&menu);
			LONGS_EQUAL(0, menu.opens);
		}
	}
}

TEST(UIOpen, close_song_reuse_stops_render_focus_and_redraw_continuation) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int boundary = 0; boundary < 3; ++boundary) {
			root = {};
			menu = {};
			navigation().depth = 2;
			navigation().hierarchy[0] = &root;
			navigation().hierarchy[1] = &menu;
			auto reuse_song = [] {
				song.~Song();
				new (&song) Song;
			};
			PadLEDs::on_greyout = {};
			if (boundary == 0)
				menu.on_main = reuse_song;
			else if (boundary == 1)
				PadLEDs::on_greyout = reuse_song;
			else
				root.on_focus = reuse_song;
			closeUI(&menu);
			LONGS_EQUAL(boundary == 0 ? 1 : 2, menu.renders);
			LONGS_EQUAL(boundary == 2 ? 1 : 0, root.focuses);
			LONGS_EQUAL(0, redraws.active());
		}
	}
}

TEST(UIOpen, retired_song_rejects_navigation_mutation) {
	Song retired_song;
	retired_song.lifetime.retire();
	currentSong = &retired_song;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		navigation().depth = 2;
		navigation().hierarchy[1] = &menu;
		CHECK_FALSE(openUI(&replacement));
		CHECK_FALSE(changeUIAtLevel(&replacement, 1));
		CHECK_FALSE(changeUISideways(&replacement));
		changeRootUI(&replacement);
		setRootUILowLevel(&replacement);
		swapOutRootUILowLevel(&replacement);
		closeUI(&menu);
		LONGS_EQUAL(2, navigation().depth);
		POINTERS_EQUAL(&root, navigation().hierarchy[0]);
		POINTERS_EQUAL(&menu, navigation().hierarchy[1]);
		LONGS_EQUAL(0, menu.renders);
	}
}

TEST(UIOpen, successful_root_open_song_reuse_does_not_schedule_redraw) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		menu.on_open = [] {
			song.~Song();
			new (&song) Song;
		};
		changeRootUI(&menu);
		POINTERS_EQUAL(&menu, getCurrentUI());
		LONGS_EQUAL(0, redraws.active());
	}
}

TEST(UIOpen, no_song_navigation_still_opens_closes_and_changes_root) {
	currentSong = nullptr;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		CHECK(openUI(&menu));
		POINTERS_EQUAL(&menu, getCurrentUI());
		closeUI(&menu);
		POINTERS_EQUAL(&root, getCurrentUI());
		changeRootUI(&replacement);
		POINTERS_EQUAL(&replacement, getCurrentUI());
	}
}

TEST(UIOpen, greyout_song_reuse_rejects_result_and_stops_lower_layer_query) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (bool used : {false, true}) {
			navigation().depth = 2;
			navigation().hierarchy[1] = &menu;
			menu.greyout_used = used;
			int lower_queries = 0;
			root.on_greyout_query = [&] { ++lower_queries; };
			menu.on_greyout_query = [] {
				song.~Song();
				new (&song) Song;
			};
			CHECK_FALSE(getUIGreyoutColsAndRows().has_value());
			LONGS_EQUAL(0, lower_queries);
		}
	}
}

TEST(UIOpen, render_request_song_reuse_does_not_query_sidebar_or_dirty_old_target) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		navigation().depth = 2;
		navigation().hierarchy[1] = &menu;
		menu.renders = 0;
		menu.on_main = [] {
			song.~Song();
			new (&song) Song;
		};
		uiNeedsRendering(&root, 1, 2);
		LONGS_EQUAL(1, menu.renders);
		LONGS_EQUAL(0, navigation().main_rows_dirty);
		LONGS_EQUAL(0, navigation().side_rows_dirty);
	}
}

TEST(UIOpen, grid_song_reuse_or_takeover_stops_publication_and_preserves_redraw) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (bool takeover : {false, true}) {
			client_mode = false;
			root.renders = 0;
			root.main_needed = true;
			navigation().main_rows_dirty = 1;
			navigation().side_rows_dirty = 2;
			root.on_main = [=] {
				if (takeover)
					client_mode = true;
				else {
					song.~Song();
					new (&song) Song;
				}
			};
			doAnyPendingGridRendering();
			LONGS_EQUAL(1, root.renders);
			LONGS_EQUAL(0, PadLEDs::main_sends.active());
			LONGS_EQUAL(0, PadLEDs::side_sends.active());
			LONGS_EQUAL(1, navigation().main_rows_dirty);
			LONGS_EQUAL(2, navigation().side_rows_dirty);
		}
	}
}

TEST(UIOpen, oled_song_reuse_stops_each_render_boundary_and_preserves_redraw) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int boundary = 0; boundary < 3; ++boundary) {
			root.oled_renders = 0;
			root.on_oled = {};
			OLED::on_clear = OLED::on_stop = {};
			navigation().oled_dirty = true;
			auto reuse_song = [] {
				song.~Song();
				new (&song) Song;
			};
			if (boundary == 0)
				OLED::on_clear = reuse_song;
			else if (boundary == 1)
				OLED::on_stop = reuse_song;
			else
				root.on_oled = reuse_song;
			doAnyPendingOLEDRendering();
			LONGS_EQUAL(boundary == 2 ? 1 : 0, root.oled_renders);
			LONGS_EQUAL(0, OLED::sends.active());
			CHECK(navigation().oled_dirty);
		}
	}
}

TEST(UIOpen, song_reuse_during_shared_refresh_cancels_render_and_keeps_refresh_pending) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		navigation().shared_model_refresh.request();
		navigation().main_rows_dirty = 1;
		navigation().oled_dirty = true;
		root.on_refresh = [] {
			song.~Song();
			new (&song) Song;
		};
		doAnyPendingUIRendering();
		CHECK_FALSE(navigation().rendering);
		LONGS_EQUAL(0, root.renders);
		LONGS_EQUAL(0, root.oled_renders);
		CHECK(navigation().shared_model_refresh.consume(0));
	}
}

TEST(UIOpen, retired_song_rejects_render_entry_points_without_consuming_requests) {
	Song retired_song;
	retired_song.lifetime.retire();
	currentSong = &retired_song;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		navigation().main_rows_dirty = 1;
		navigation().side_rows_dirty = 2;
		navigation().oled_dirty = true;
		CHECK_FALSE(getUIGreyoutColsAndRows().has_value());
		uiNeedsRendering(&root, 4, 8);
		doAnyPendingGridRendering();
		doAnyPendingOLEDRendering();
		doAnyPendingUIRendering();
		LONGS_EQUAL(0, root.renders);
		LONGS_EQUAL(0, root.oled_renders);
		LONGS_EQUAL(0, OLED::sends.active());
		LONGS_EQUAL(1, navigation().main_rows_dirty);
		LONGS_EQUAL(2, navigation().side_rows_dirty);
		CHECK(navigation().oled_dirty);
	}
}

TEST(UIOpen, grid_send_song_reuse_stops_sidebar_and_outer_oled_render) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		root.renders = 0;
		root.main_needed = true;
		navigation().main_rows_dirty = 1;
		navigation().side_rows_dirty = 2;
		navigation().oled_dirty = true;
		PadLEDs::on_main_send = [] {
			song.~Song();
			new (&song) Song;
		};
		doAnyPendingUIRendering();
		LONGS_EQUAL(1, root.renders);
		LONGS_EQUAL(1, PadLEDs::main_sends.active());
		LONGS_EQUAL(0, PadLEDs::side_sends.active());
		LONGS_EQUAL(0, root.oled_renders);
		CHECK(navigation().oled_dirty);
		CHECK_FALSE(navigation().rendering);
	}
}

TEST(UIOpen, oled_takeover_does_not_publish_local_image_over_mirror_frame) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		client_mode = false;
		navigation().oled_dirty = true;
		root.on_oled = [] { client_mode = true; };
		doAnyPendingOLEDRendering();
		LONGS_EQUAL(0, OLED::sends.active());
		CHECK(navigation().oled_dirty);
	}
}

TEST(UIOpen, client_direct_render_entry_preserves_pending_requests) {
	client_mode = true;
	navigation().main_rows_dirty = 1;
	navigation().side_rows_dirty = 2;
	navigation().oled_dirty = true;
	doAnyPendingGridRendering();
	doAnyPendingOLEDRendering();
	LONGS_EQUAL(0, root.renders);
	LONGS_EQUAL(0, OLED::clears.active());
	LONGS_EQUAL(0, OLED::sends.active());
	LONGS_EQUAL(1, navigation().main_rows_dirty);
	LONGS_EQUAL(2, navigation().side_rows_dirty);
	CHECK(navigation().oled_dirty);
}

TEST(UIOpen, no_song_context_can_query_and_render) {
	currentSong = nullptr;
	root.main_needed = root.side_needed = root.greyout_used = true;
	CHECK(getUIGreyoutColsAndRows().has_value());
	uiNeedsRendering(&root, 1, 2);
	navigation().oled_dirty = true;
	doAnyPendingUIRendering();
	LONGS_EQUAL(1, PadLEDs::main_sends.active());
	LONGS_EQUAL(1, PadLEDs::side_sends.active());
	LONGS_EQUAL(1, OLED::sends.active());
	CHECK_FALSE(navigation().oled_dirty);
}

TEST(UIOpen, loaded_song_root_setup_cancellation_does_not_open_old_or_replacement_ui) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int boundary = 0; boundary < 3; ++boundary) {
			navigation().depth = 1;
			navigation().hierarchy[0] = &root;
			overview_ui = {};
			overview_ui.redirected = &overview_ui;
			PadLEDs::on_greyout = {};
			if (boundary == 0)
				overview_ui.redirected = nullptr;
			else if (boundary == 1)
				overview_ui.on_resolve = [] {
					song.~Song();
					new (&song) Song;
				};
			else
				PadLEDs::on_greyout = [] {
					song.~Song();
					new (&song) Song;
				};
			setUIForLoadedSong(&song);
			LONGS_EQUAL(0, root.opens);
			LONGS_EQUAL(0, overview_ui.opens);
			LONGS_EQUAL(0, redraws.active());
		}
	}
}

TEST(UIOpen, loaded_song_open_invalidation_does_not_schedule_redraw) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int invalidation = 0; invalidation < 3; ++invalidation) {
			overview_ui.on_open = [=, this] {
				if (invalidation == 0) {
					song.~Song();
					new (&song) Song;
				}
				else if (invalidation == 1)
					navigation().hierarchy[0] = &replacement;
				else
					session::detail::active = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
			};
			setUIForLoadedSong(&song);
			CHECK(session::current() == owner);
			LONGS_EQUAL(0, redraws.for_owner(session::Id::Local));
			LONGS_EQUAL(0, redraws.for_owner(session::Id::Remote));
		}
	}
}

TEST(UIOpen, loaded_song_view_construction_song_reuse_does_not_install_root) {
	InstrumentClip clip;
	song.clip = &clip;
	song.inClipMinderViewOnLoad = true;
	on_view_access = [] {
		song.~Song();
		new (&song) Song;
	};
	setUIForLoadedSong(&song);
	POINTERS_EQUAL(&root, getCurrentUI());
	LONGS_EQUAL(0, instrument_ui.opens);
}

TEST(UIOpen, loaded_song_selects_and_opens_each_supported_root) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		for (int selection = 0; selection < 6; ++selection) {
			InstrumentClip clip;
			song.clip = selection < 4 ? &clip : nullptr;
			song.inClipMinderViewOnLoad = true;
			song.last_instance = selection == 4 ? 0 : -1;
			clip.automation = selection == 0;
			clip.keyboard = selection == 1;
			clip.type = selection == 3 ? ClipType::AUDIO : ClipType::INSTRUMENT;
			ui_open_test::UI* targets[] = {&automation_ui, &keyboard_ui, &instrument_ui,
			                               &audio_ui,      &arranger_ui, &overview_ui};
			auto* target = targets[selection];
			const int previous_opens = target->opens;
			setUIForLoadedSong(&song);
			POINTERS_EQUAL(target, getCurrentUI());
			LONGS_EQUAL(previous_opens + 1, target->opens);
		}
	}
}

TEST(UIOpen, loaded_song_rejects_null_retired_and_noncurrent_song) {
	Song other_song;
	setUIForLoadedSong(nullptr);
	setUIForLoadedSong(&other_song);
	other_song.lifetime.retire();
	currentSong = &other_song;
	setUIForLoadedSong(&other_song);
	POINTERS_EQUAL(&root, getCurrentUI());
	LONGS_EQUAL(0, overview_ui.opens);
	LONGS_EQUAL(0, redraws.active());
}

TEST(UIOpen, loaded_song_client_takeover_during_open_skips_redraw) {
	overview_ui.on_open = [] { client_mode = true; };
	setUIForLoadedSong(&song);
	LONGS_EQUAL(1, overview_ui.opens);
	LONGS_EQUAL(0, redraws.active());
}
