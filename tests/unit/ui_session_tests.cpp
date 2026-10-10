#include "CppUTest/TestHarness.h"
#include "gui/menu_item/session_menu_entries.h"
#include "gui/menu_item/shared_value_cache.h"
#include "gui/ui/deferred_session_command.h"
#include "gui/ui/graphics_routing.h"
#include "gui/ui/recording_session.h"
#include "gui/ui/ui_navigation_state.h"
#include "gui/ui_timer_state.h"
#include "hid/display/oled_frame_state.h"
#include "hid/display/seven_segment_frame.h"
#include "hid/encoder_acceleration.h"
#include "hid/encoder_input_state.h"
#include "hid/led/indicator_leds_state.h"
#include "hid/led/pad_leds_state.h"
#include "model/action/reversible_shift.h"
#include "model/action/reversion_guard.h"
#include "model/action/session_history.h"
#include "model/clip/clip_view_state.h"
#include "model/song/song_clip_selection.h"
#include "model/song/song_navigation_state.h"
#include "modulation/automation/parameter_revision.h"
#include <cmath>
#include <memory>
#include <type_traits>
#include <vector>

using namespace deluge::gui::ui_session;

TEST_GROUP(UISession){};

TEST(UISession, horizontal_shift_rejects_overflow_before_narrowing_or_undo_negation) {
	using namespace deluge::model;
	constexpr int32_t max = std::numeric_limits<int32_t>::max();
	constexpr int32_t min = std::numeric_limits<int32_t>::min();
	CHECK_TRUE(is_reversible_shift(max));
	CHECK_TRUE(is_reversible_shift(-max));
	CHECK_FALSE(is_reversible_shift(min));
	CHECK_FALSE(is_reversible_shift(static_cast<int64_t>(max) + 1));
	CHECK_FALSE(is_reversible_shift(std::numeric_limits<int64_t>::min()));
	CHECK_FALSE(is_reversible_shift(std::numeric_limits<int64_t>::max()));
	LONGS_EQUAL(max, *horizontal_shift_amount(max, 0, 1));
	LONGS_EQUAL(-max, *horizontal_shift_amount(-max, 0, 1));
	CHECK_FALSE(horizontal_shift_amount(min, 0, 1).has_value());
	CHECK_FALSE(horizontal_shift_amount(max, 0, 2).has_value());
	CHECK_FALSE(horizontal_shift_amount(min, 0, 2).has_value());
	CHECK_FALSE(horizontal_shift_amount(max, min, max).has_value());
	CHECK_FALSE(horizontal_shift_amount(min, min, max).has_value());
	CHECK_FALSE(horizontal_shift_amount(1, 10, 10).has_value());
	CHECK_FALSE(horizontal_shift_amount(1, 10, 9).has_value());
	LONGS_EQUAL(0, *horizontal_shift_amount(0, min, max));
	for (int width : {1, 3, 12, 96, 1536}) {
		for (int delta = -127; delta <= 127; ++delta) {
			auto result = horizontal_shift_amount(delta, -12, width - 12);
			CHECK_TRUE(result.has_value());
			LONGS_EQUAL(delta * width, *result);
			LONGS_EQUAL(-delta * width, -*result);
		}
	}
}

namespace {
struct OwnedEditor {
	Id constructed_for = current();
	std::unique_ptr<int> selection = std::make_unique<int>(0);
};

struct CountedEditor {
	inline static unsigned constructions[2]{};
	CountedEditor() { ++constructions[static_cast<size_t>(current())]; }
};

struct NamedEditor {
	explicit NamedEditor(std::unique_ptr<int> configuration) : configuration(std::move(configuration)) {}
	std::unique_ptr<int> configuration;
};
} // namespace

TEST(UISession, native_editor_construction_forwards_owned_arguments_only_once) {
	NamedEditor local(std::make_unique<int>(12));
	RemoteInstance<NamedEditor> instances;
	auto configuration = std::make_unique<int>(27);
	POINTERS_EQUAL(&local, &instances.get(local, std::move(configuration)));
	CHECK(configuration != nullptr); // Local lookup does not consume Remote constructor arguments.
	{
		Scope remote(Id::Remote);
		auto& editor = instances.get(local, std::move(configuration));
		CHECK_FALSE(configuration);
		CHECK_EQUAL(27, *editor.configuration);
		configuration = std::make_unique<int>(42);
		POINTERS_EQUAL(&editor, &instances.get(local, std::move(configuration)));
		CHECK(configuration != nullptr); // Existing Remote instances keep their identity and configuration.
		CHECK_EQUAL(27, *editor.configuration);
	}
	CHECK_EQUAL(12, *local.configuration);
}

TEST(UISession, local_access_does_not_construct_remote_editor) {
	CountedEditor::constructions[0] = 0;
	CountedEditor::constructions[1] = 0;
	CountedEditor local;
	RemoteInstance<CountedEditor> instances;
	instances.get(local);
	instances.get(local);
	CHECK_EQUAL(1, CountedEditor::constructions[0]);
	CHECK_EQUAL(0, CountedEditor::constructions[1]);
	{
		Scope remote(Id::Remote);
		instances.get(local);
		instances.get(local);
	}
	CHECK_EQUAL(1, CountedEditor::constructions[0]);
	CHECK_EQUAL(1, CountedEditor::constructions[1]);
}

TEST(UISession, native_editor_instances_keep_identity_and_owned_state) {
	OwnedEditor local;
	RemoteInstance<OwnedEditor> instances;
	POINTERS_EQUAL(&local, &instances.get(local));
	*local.selection = 12;
	OwnedEditor* remote;
	{
		Scope scope(Id::Remote);
		remote = &instances.get(local);
		CHECK(remote != &local);
		CHECK(remote->constructed_for == Id::Remote);
		CHECK_EQUAL(0, *remote->selection);
		*remote->selection = 27;
		{
			Scope hardware(Id::Local);
			POINTERS_EQUAL(&local, &instances.get(local));
			CHECK_EQUAL(12, *instances.get(local).selection);
		}
		POINTERS_EQUAL(remote, &instances.get(local));
	}
	CHECK(local.constructed_for == Id::Local);
	CHECK_EQUAL(12, *local.selection);
	{
		Scope scope(Id::Remote);
		POINTERS_EQUAL(remote, &instances.get(local));
		CHECK_EQUAL(27, *instances.get(local).selection);
	}
}

TEST(UISession, nested_hardware_service_restores_suspended_remote_owner) {
	CHECK(current() == Id::Local);
	{
		Scope remote(Id::Remote);
		CHECK(current() == Id::Remote);
		{
			Scope hardware(Id::Local);
			CHECK(current() == Id::Local);
		}
		CHECK(current() == Id::Remote);
	}
	CHECK(current() == Id::Local);
}

TEST(UISession, session_containers_have_independent_ownership) {
	State<std::unique_ptr<int>> state;
	state.for_owner(Id::Local) = std::make_unique<int>(17);
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(state.active());
		state.active() = std::make_unique<int>(42);
		CHECK_EQUAL(42, *state.active());
	}
	CHECK_EQUAL(17, *state.active());
}

TEST(UISession, navigation_mode_and_dirty_flags_stay_with_their_screen) {
	State<deluge::gui::ui_session::Navigation> state;
	auto& local = state.active();
	local.depth = 3;
	local.mode = 0x21;
	local.oled_dirty = true;
	local.main_rows_dirty = 4;
	local.rendering = true;
	{
		Scope remote(Id::Remote);
		CHECK_EQUAL(0, state.active().depth);
		CHECK_FALSE(state.active().rendering);
		CHECK_FALSE(state.active().oled_dirty);
		state.active().depth = 1;
		state.active().mode = 7;
		state.active().side_rows_dirty = 128;
	}
	CHECK_EQUAL(3, local.depth);
	CHECK_EQUAL(0x21, local.mode);
	CHECK_EQUAL(4, local.main_rows_dirty);
	CHECK_EQUAL(0, local.side_rows_dirty);
	CHECK_TRUE(local.oled_dirty);
	CHECK_TRUE(local.rendering);
}

TEST(UISession, identically_named_timers_do_not_cancel_each_other) {
	UITimerState timers;
	timers.set(TimerName::UI_SPECIFIC, 100, 40);
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(timers.get(TimerName::UI_SPECIFIC).active);
		timers.set(TimerName::UI_SPECIFIC, 100, 10);
		CHECK_EQUAL(110, timers.active_bank().next_event);
		timers.unset(TimerName::UI_SPECIFIC, 101);
	}
	CHECK_TRUE(timers.get(TimerName::UI_SPECIFIC).active);
	CHECK_EQUAL(140, timers.get(TimerName::UI_SPECIFIC).triggerTime);
	CHECK_EQUAL(140, timers.active_bank().next_event);
}

TEST(UISession, hardware_timer_from_remote_targets_local_bank_and_deadline) {
	UITimerState timers;
	{
		Scope remote(Id::Remote);
		timers.set(TimerName::OLED_LOW_LEVEL, 500, 30);
		CHECK_EQUAL(530, timers.bank(Id::Local).next_event);
		CHECK_FALSE(timers.active_bank().timers[static_cast<size_t>(TimerName::OLED_LOW_LEVEL)].active);
		timers.set(TimerName::DISPLAY, 500, 90);
		CHECK_EQUAL(590, timers.active_bank().next_event);
	}
	CHECK_TRUE(timers.get(TimerName::OLED_LOW_LEVEL).active);
	CHECK_FALSE(timers.get(TimerName::DISPLAY).active);
}

TEST(UISession, timer_deadlines_remain_ordered_across_sample_clock_wraparound) {
	UITimerState timers;
	constexpr uint32_t now = 0xfffffff0U;
	timers.recompute(Id::Local, now);
	timers.set(TimerName::DISPLAY, now, 32);
	timers.set(TimerName::UI_SPECIFIC, now, 8);
	CHECK_EQUAL(0xfffffff8U, timers.active_bank().next_event);
	timers.unset(TimerName::UI_SPECIFIC, now);
	CHECK_EQUAL(16, timers.active_bank().next_event);
	CHECK_EQUAL(16, timers.get(TimerName::DISPLAY).triggerTime);
}

TEST(UISession, following_timer_recomputes_the_destination_sessions_deadline) {
	UITimerState timers;
	timers.set(TimerName::OLED_LOW_LEVEL, 100, 15);
	{
		Scope remote(Id::Remote);
		timers.set(TimerName::DISPLAY, 100, 500);
		timers.follow(TimerName::UI_SPECIFIC, TimerName::OLED_LOW_LEVEL, 100);
		CHECK_EQUAL(115, timers.active_bank().next_event);
	}
	CHECK_FALSE(timers.get(TimerName::UI_SPECIFIC).active);
}

TEST(UISession, mirror_pause_shifts_local_ui_timers_but_not_hardware_handshake_or_remote_timers) {
	UITimerState timers;
	timers.set(TimerName::DISPLAY, 10, 100);
	timers.set(TimerName::OLED_LOW_LEVEL, 10, 30);
	{
		Scope remote(Id::Remote);
		timers.set(TimerName::DISPLAY, 10, 80);
	}
	timers.pause_local(20);
	timers.resume_local(2020);
	CHECK_EQUAL(2110, timers.get(TimerName::DISPLAY).triggerTime);
	CHECK_EQUAL(40, timers.get(TimerName::OLED_LOW_LEVEL).triggerTime);
	{
		Scope remote(Id::Remote);
		CHECK_EQUAL(90, timers.get(TimerName::DISPLAY).triggerTime);
	}
}

TEST(UISession, recording_reservation_survives_yield_and_can_be_reused_after_completion) {
	RecordingSession recording;
	{
		Scope remote(Id::Remote);
		CHECK_TRUE(recording.acquire());
	}
	CHECK_TRUE(recording.active());
	CHECK_FALSE(recording.acquire());
	CHECK_TRUE(recording.owner() == Id::Remote);
	{
		Scope completion(recording.owner());
		CHECK_TRUE(current() == Id::Remote);
		recording.release();
	}
	CHECK_TRUE(current() == Id::Local);
	CHECK_TRUE(recording.acquire());
	CHECK_TRUE(recording.owner() == Id::Local);
}


#include "model/clip/sample_shift.h"
TEST(UISession, sample_shift_checks_bounds_and_overflow_before_mutation) {
	using deluge::model::shifted_sample_start;
	constexpr auto max = std::numeric_limits<uint64_t>::max();
	CHECK_FALSE(shifted_sample_start(10, 20, 100, 1, 0).has_value());
	CHECK_FALSE(shifted_sample_start(10, 20, 100, 1, -1).has_value());
	CHECK_FALSE(shifted_sample_start(20, 10, 100, 1, 1).has_value());
	CHECK_FALSE(shifted_sample_start(10, 30, 100, 1, 1).has_value());
	CHECK_FALSE(shifted_sample_start(90, 110, 100, -1, 1).has_value());
	CHECK_FALSE(shifted_sample_start(0, max, max, 2, 1).has_value());
	CHECK_FALSE(shifted_sample_start(max - 1, max, max, -2, 1).has_value());
	CHECK_FALSE(shifted_sample_start(max - 1, max, max, -1, 1).has_value());
	auto edge = shifted_sample_start(10, 20, 20, -1, 1);
	CHECK_TRUE(edge.has_value());
	UNSIGNED_LONGS_EQUAL(20, *edge);
	auto min_tick = shifted_sample_start(0, 1, max, std::numeric_limits<int32_t>::min(), 1);
	CHECK_TRUE(min_tick.has_value());
	CHECK_TRUE(*min_tick == uint64_t{2147483648});
	for (int length : {1, 3, 96}) {
		for (int ticks = -127; ticks <= 127; ++ticks) {
			const int64_t expected = 100000 - static_cast<int64_t>(ticks) * 1000 / length;
			auto result = shifted_sample_start(100000, 101000, 200000, ticks, length);
			bool valid = expected >= 0 && expected <= 200000;
			CHECK_EQUAL(valid, result.has_value());
			if (valid)
				CHECK_TRUE(*result == static_cast<uint64_t>(expected));
		}
	}
}

