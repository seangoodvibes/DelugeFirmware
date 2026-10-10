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
#include "model/action/action_clip_state.h"
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

TEST(UISession, encoder_acceleration_resets_on_reversal_pause_and_clock_reset) {
	deluge::hid::encoders::EncoderAcceleration encoder;
	DOUBLES_EQUAL(1.0, encoder.advance(1, 1.0), 0.0001);
	CHECK(encoder.advance(1, 1.01) > 1.0);
	CHECK(std::isfinite(encoder.advance(1, 1.01)));
	DOUBLES_EQUAL(1.0, encoder.advance(-1, 1.02), 0.0001);
	CHECK(encoder.advance(-1, 1.03) > 1.0);
	DOUBLES_EQUAL(1.0, encoder.advance(-1, 2.0), 0.0001);
	DOUBLES_EQUAL(1.0, encoder.advance(-1, 0.0), 0.0001);
	for (int i = 1; i <= 100; ++i)
		encoder.advance(-1, i * 0.001);
	DOUBLES_EQUAL(3.0, encoder.advance(-1, 0.101), 0.0001);
}

TEST(UISession, encoder_histories_are_independent_across_panels_and_knobs) {
	State<deluge::hid::encoders::EncoderAcceleration> first;
	State<deluge::hid::encoders::EncoderAcceleration> second;
	first.active().advance(1, 1.0);
	CHECK(first.active().advance(1, 1.01) > 1.0);
	DOUBLES_EQUAL(1.0, second.active().advance(1, 1.01), 0.0001);
	{
		Scope remote(Id::Remote);
		DOUBLES_EQUAL(1.0, first.active().advance(1, 1.01), 0.0001);
	}
	CHECK(first.active().advance(1, 1.02) > 1.0);
}

TEST(UISession, encoder_card_deferral_and_turn_windows_belong_to_initiating_panel) {
	State<deluge::hid::encoders::InputState> inputs;
	inputs.active().waiting_for_card_routine_end = 8;
	inputs.active().timeModEncoderLastTurned[0] = 123;
	{
		Scope remote(Id::Remote);
		CHECK_EQUAL(0, inputs.active().waiting_for_card_routine_end);
		CHECK_EQUAL(0, inputs.active().timeModEncoderLastTurned[0]);
		inputs.active().initial_turn_direction[1] = -1;
	}
	CHECK_EQUAL(8, inputs.active().waiting_for_card_routine_end);
	CHECK_EQUAL(123, inputs.active().timeModEncoderLastTurned[0]);
	CHECK_EQUAL(0, inputs.active().initial_turn_direction[1]);
}

TEST(UISession, menu_reordering_and_cursor_ownership_remain_independent) {
	int bass = 10, frequency = 20, treble = 30;
	std::array<int*, 3> declaration{&bass, &frequency, &treble};
	deluge::gui::menu_item::SessionMenuEntries<std::vector<int*>> entries(declaration);
	auto& local = entries.active();
	CHECK(local.current == local.items.end());
	local.current = local.items.begin() + 1;
	local.initial_selection_pending = false;
	std::swap(local.items[0], local.items[1]);
	{
		Scope remote(Id::Remote);
		auto& remote_entries = entries.active();
		CHECK(remote_entries.current == remote_entries.items.end());
		CHECK_TRUE(remote_entries.initial_selection_pending);
		POINTERS_EQUAL(&bass, remote_entries.items[0]);
		CHECK(remote_entries.items.data() != local.items.data());
		remote_entries.current = remote_entries.items.begin() + 2;
		**remote_entries.current = 42; // Musical objects remain shared.
		std::swap(remote_entries.items[1], remote_entries.items[2]);
	}
	POINTERS_EQUAL(&bass, *local.current);
	POINTERS_EQUAL(&treble, local.items[2]);
	CHECK_EQUAL(42, *local.items[2]);
	CHECK_FALSE(local.initial_selection_pending);
}

TEST(UISession, empty_menu_starts_with_valid_end_cursor_in_both_panels) {
	deluge::gui::menu_item::SessionMenuEntries<std::vector<int*>> entries(std::span<int* const>{});
	CHECK(entries.active().current == entries.active().items.end());
	Scope remote(Id::Remote);
	CHECK(entries.active().current == entries.active().items.end());
}

TEST(UISession, shared_setting_edits_apply_to_latest_committed_value_from_either_panel) {
	deluge::gui::menu_item::SharedValueCache<int> cache;
	int model = 40;
	auto read = [&] { return cache.get([&] { cache.set(model); }); };
	auto turn = [&](int offset) {
		cache.set(read() + offset);
		model = read();
		cache.committed();
	};
	cache.set(model);
	{
		Scope remote(Id::Remote);
		cache.set(model);
	}
	turn(1);
	CHECK_EQUAL(41, model);
	{
		Scope remote(Id::Remote);
		turn(1);
		CHECK_EQUAL(42, model);
	}
	CHECK_EQUAL(42, read());
	turn(-1);
	{
		Scope remote(Id::Remote);
		CHECK_EQUAL(41, read());
	}
}

TEST(UISession, shared_menu_cache_reload_uses_the_reading_panels_model_target) {
	deluge::gui::menu_item::SharedValueCache<int> cache;
	State<int> models;
	models.active() = 12;
	cache.set(12);
	{
		Scope remote(Id::Remote);
		models.active() = 80;
		cache.set(80);
	}
	models.active() = 13;
	cache.set(13);
	cache.committed();
	{
		Scope remote(Id::Remote);
		CHECK_EQUAL(80, cache.get([&] { cache.set(models.active()); }));
	}
	CHECK_EQUAL(13, cache.get([&] { FAIL("Writer cache should remain valid"); }));
}

TEST(UISession, shared_value_cache_reloads_once_and_allows_recursive_reads) {
	deluge::gui::menu_item::SharedValueCache<int> cache;
	{
		Scope remote(Id::Remote);
		cache.set(5);
	}
	cache.committed();
	cache.committed();
	Scope remote(Id::Remote);
	int reloads = 0;
	auto reload = [&] {
		++reloads;
		CHECK_EQUAL(5, cache.get([&] { FAIL("Recursive reload"); }));
		cache.set(9);
	};
	CHECK_EQUAL(9, cache.get(reload));
	CHECK_EQUAL(9, cache.get(reload));
	CHECK_EQUAL(1, reloads);
}

TEST(UISession, parameter_aliases_reload_after_shared_model_edit) {
	using namespace deluge::modulation::automation;
	deluge::gui::menu_item::SharedValueCache<int> first, alias;
	int model = 30;
	first.set(model, parameter_revision);
	{
		Scope remote(Id::Remote);
		alias.set(model, parameter_revision);
	}
	{
		ParameterEditRevision edit;
		model = 31;
	}
	{
		Scope remote(Id::Remote);
		int value = alias.get([&] { alias.set(model, parameter_revision); }, parameter_revision);
		CHECK_EQUAL(31, value);
		{
			ParameterEditRevision edit;
			model = value + 1;
		}
	}
	CHECK_EQUAL(32, first.get([&] { first.set(model, parameter_revision); }, parameter_revision));
}

TEST(UISession, model_revision_does_not_discard_unrelated_menu_drafts) {
	using namespace deluge::modulation::automation;
	deluge::gui::menu_item::SharedValueCache<int> draft;
	draft.set(17);
	{ ParameterEditRevision edit; }
	CHECK_EQUAL(17, draft.get([&] { FAIL("Draft must not follow parameter revisions"); }));
}

TEST(UISession, model_revision_publishes_on_scope_exit_including_nested_edits) {
	using namespace deluge::modulation::automation;
	uint64_t before = parameter_revision;
	{
		ParameterEditRevision outer;
		CHECK(parameter_revision == before);
		{ ParameterEditRevision inner; }
		CHECK(parameter_revision == before + 1);
	}
	CHECK(parameter_revision == before + 2);
}

TEST(UISession, shared_refresh_notifications_coalesce_and_remain_independent) {
	State<SharedModelRefresh> refresh;
	refresh.active().request();
	refresh.active().request();
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(refresh.active().consume(0));
	}
	CHECK_TRUE(refresh.active().consume(0));
	CHECK_FALSE(refresh.active().consume(0));
	CHECK_TRUE(refresh.active().consume(3));
	{
		Scope remote(Id::Remote);
		CHECK_TRUE(refresh.active().consume(3));
		CHECK_FALSE(refresh.active().consume(3));
	}
}

TEST(UISession, shared_refresh_retains_changes_made_during_callback) {
	SharedModelRefresh refresh;
	refresh.request();
	CHECK_TRUE(refresh.consume(4));
	refresh.request(); // Notification from a nested operation in the refresh callback.
	CHECK_TRUE(refresh.consume(4));
	CHECK_FALSE(refresh.consume(4));
	CHECK_TRUE(refresh.consume(5));
}

TEST(UISession, shared_refresh_does_not_consume_stale_cache_until_owner_reads_it) {
	deluge::gui::menu_item::SharedValueCache<int> cache;
	cache.set(12, 7);
	CHECK_FALSE(cache.needs_reload(7));
	CHECK_TRUE(cache.needs_reload(8));
	CHECK_TRUE(cache.needs_reload(8));
	CHECK_EQUAL(14, cache.get([&] { cache.set(14, 8); }, 8));
	CHECK_FALSE(cache.needs_reload(8));
	{
		Scope remote(Id::Remote);
		cache.committed();
	}
	CHECK_TRUE(cache.needs_reload(8));
}

TEST(UISession, oled_working_canvases_and_published_frames_are_independent) {
	State<deluge::hid::display::OLEDFrameState> frames;
	auto& local = frames.active();
	local.main.clear();
	local.main.hackGetImageStore()[0][0] = 0x12;
	local.current_image = local.main.hackGetImageStore();
	local.needsSending = true;
	{
		Scope remote(Id::Remote);
		auto& frame = frames.active();
		CHECK(frame.current_image == nullptr);
		CHECK_FALSE(frame.needsSending);
		frame.popup.clear();
		frame.popup.hackGetImageStore()[0][0] = 0xA5;
		frame.current_image = frame.popup.hackGetImageStore();
		frame.publish();
		CHECK_EQUAL(1, frame.revision);
		POINTERS_EQUAL(frame.completed.hackGetImageStore(), frame.current_image);
		frame.popup.clear();
		CHECK_EQUAL(0xA5, frame.current_image[0][0]);
	}
	CHECK_EQUAL(0x12, local.current_image[0][0]);
	CHECK_TRUE(local.needsSending);
	CHECK_EQUAL(0, local.revision);
}

TEST(UISession, oled_publish_requires_an_image_and_replaces_the_previous_snapshot) {
	deluge::hid::display::OLEDFrameState frame{};
	frame.publish();
	CHECK_EQUAL(0, frame.revision);
	frame.main.clear();
	frame.main.hackGetImageStore()[0][0] = 3;
	frame.current_image = frame.main.hackGetImageStore();
	frame.publish();
	frame.console.clear();
	frame.console.hackGetImageStore()[0][0] = 9;
	frame.current_image = frame.console.hackGetImageStore();
	frame.publish();
	CHECK_EQUAL(2, frame.revision);
	CHECK_EQUAL(9, frame.current_image[0][0]);
	CHECK_EQUAL(3, frame.main.hackGetImageStore()[0][0]);
}

TEST(UISession, numeric_frames_include_fixed_dots_without_changing_layer_bytes) {
	deluge::hid::display::SevenSegmentFrame frame;
	std::array<uint8_t, kNumericDisplayLength> layer{1, 2, 4, 8};
	frame.publish(layer, 2);
	CHECK_EQUAL(0x84, frame.segments[2]);
	CHECK_EQUAL(4, layer[2]);
	CHECK_EQUAL(1, frame.revision);
	frame.publish(layer, 0x89); // Dots at both ends.
	CHECK_EQUAL(0x81, frame.segments[0]);
	CHECK_EQUAL(2, frame.segments[1]);
	CHECK_EQUAL(4, frame.segments[2]);
	CHECK_EQUAL(0x88, frame.segments[3]);
	frame.publish(layer);
	CHECK_EQUAL(8, frame.segments[3]);
	CHECK_EQUAL(3, frame.revision);
}

TEST(UISession, numeric_frames_remain_independent_across_panels) {
	State<deluge::hid::display::SevenSegmentFrame> frames;
	frames.active().publish({1, 2, 3, 4});
	{
		Scope remote(Id::Remote);
		CHECK_EQUAL(0, frames.active().revision);
		frames.active().publish({4, 3, 2, 1}, 0);
		CHECK_EQUAL(0x84, frames.active().segments[0]);
	}
	CHECK_EQUAL(1, frames.active().revision);
	CHECK_EQUAL(1, frames.active().segments[0]);
	CHECK_EQUAL(4, frames.active().segments[3]);
}

TEST(UISession, indicator_frames_capture_actual_outputs_and_ignore_identical_updates) {
	indicator_leds::IndicatorFrame frame;
	frame.set_led(0, true);
	CHECK_EQUAL(1, frame.revision);
	frame.set_led(0, true);
	CHECK_EQUAL(1, frame.revision);
	std::array<uint8_t, kNumGoldKnobIndicatorLEDs> brightness{0, 32, 255, 0};
	frame.set_knob(1, brightness);
	CHECK_EQUAL(2, frame.revision);
	frame.set_knob(1, brightness);
	CHECK_EQUAL(2, frame.revision);
	brightness[1] = 0;
	CHECK_EQUAL(32, frame.knobs[1][1]);
	frame.set_led(0, false);
	CHECK_EQUAL(3, frame.revision);
}

TEST(UISession, indicator_blink_and_meter_state_belong_to_each_panel) {
	State<indicator_leds::IndicatorState> indicators;
	auto& local = indicators.active();
	local.ledBlinkers[0].active = true;
	local.ledBlinkers[0].blinksLeft = 6;
	local.ledBlinkState[0] = true;
	local.whichKnobMetering = 1;
	local.frame.set_led(0, true);
	{
		Scope remote(Id::Remote);
		auto& state = indicators.active();
		CHECK_FALSE(state.ledBlinkers[0].active);
		CHECK_FALSE(state.ledBlinkState[0]);
		CHECK_EQUAL(0, state.whichKnobMetering);
		state.levelIndicatorBlinksLeft = 26;
		state.frame.set_knob(0, {255, 255, 0, 0});
	}
	CHECK_EQUAL(6, local.ledBlinkers[0].blinksLeft);
	CHECK_TRUE(local.ledBlinkState[0]);
	CHECK_EQUAL(0, local.levelIndicatorBlinksLeft);
	CHECK_EQUAL(0, local.frame.knobs[0][0]);
	CHECK_TRUE(local.frame.leds[0]);
}

TEST(UISession, pad_working_images_transitions_and_tick_state_are_independent) {
	State<PadLEDs::PadState> pads;
	auto& local = pads.active();
	local.image[0][0] = RGB(1, 2, 3);
	local.imageStore[0][0] = RGB(4, 5, 6);
	local.occupancyMask[0][0] = 64;
	local.horizontal.squaresScrolled = 3;
	{
		Scope remote(Id::Remote);
		auto& remote_pads = pads.active();
		CHECK_EQUAL(0, remote_pads.image[0][0].r);
		CHECK_EQUAL(0, remote_pads.occupancyMask[0][0]);
		CHECK_EQUAL(0, remote_pads.horizontal.squaresScrolled);
		CHECK_EQUAL(255, remote_pads.slowFlashSquares[0]);
		remote_pads.imageStore[0][0] = RGB(9, 9, 9);
		remote_pads.renderingLock = true;
	}
	CHECK_EQUAL(4, local.imageStore[0][0].r);
	CHECK_FALSE(local.renderingLock);
	CHECK_EQUAL(64, local.occupancyMask[0][0]);
}

TEST(UISession, pad_frames_retain_prepared_colours_and_repeated_flash_requests) {
	PadLEDs::PadFrame frame;
	std::array<RGB, kDisplayHeight * 2> columns{};
	columns[0] = RGB(9, 8, 7);
	columns[kDisplayHeight] = RGB(1, 2, 3);
	frame.set_columns(2, columns);
	CHECK_EQUAL(9, frame.colours[0][2].r);
	CHECK_EQUAL(1, frame.colours[0][3].r);
	CHECK_EQUAL(1, frame.revision);
	frame.set_columns(2, columns);
	CHECK_EQUAL(1, frame.revision);
	columns[0] = RGB(0, 0, 0);
	CHECK_EQUAL(9, frame.colours[0][2].r);
	frame.flash(2, 0, 1);
	CHECK_EQUAL(2, frame.flashes[0][2].revision);
	frame.flash(2, 0, 1);
	CHECK_EQUAL(3, frame.flashes[0][2].revision);
	CHECK_EQUAL(1, frame.flashes[0][2].colour);
}

TEST(UISession, pad_snapshots_reject_out_of_range_columns_and_flashes) {
	PadLEDs::PadFrame frame;
	std::array<RGB, kDisplayHeight * 2> columns{};
	frame.set_columns(-1, columns);
	frame.set_columns(kDisplayWidth + kSideBarWidth - 1, columns);
	frame.flash(-1, 0, 1);
	frame.flash(kDisplayWidth, 0, 1);
	frame.flash(0, kDisplayHeight, 1);
	CHECK_EQUAL(0, frame.revision);
}

TEST(UISession, song_clip_selections_are_independent_and_replacement_updates_both_panels) {
	SongClipSelection selection;
	int first_storage, second_storage, replacement_storage;
	auto* first = reinterpret_cast<Clip*>(&first_storage);
	auto* second = reinterpret_cast<Clip*>(&second_storage);
	auto* replacement = reinterpret_cast<Clip*>(&replacement_storage);
	selection.select(first);
	{
		Scope remote(Id::Remote);
		POINTERS_EQUAL(nullptr, selection.current());
		selection.select(first);
		selection.select(second);
		POINTERS_EQUAL(first, selection.previous());
		selection.replace(first, replacement);
		POINTERS_EQUAL(second, selection.current());
		POINTERS_EQUAL(replacement, selection.previous());
	}
	POINTERS_EQUAL(replacement, selection.current());
	selection.replace(replacement, nullptr);
	POINTERS_EQUAL(nullptr, selection.current());
	{
		Scope remote(Id::Remote);
		POINTERS_EQUAL(second, selection.current());
		POINTERS_EQUAL(nullptr, selection.previous());
		selection.replace(second, nullptr);
		POINTERS_EQUAL(nullptr, selection.current());
	}
}

TEST(UISession, clearing_one_panel_selection_does_not_clear_the_other_panel) {
	SongClipSelection selection;
	int storage;
	auto* clip = reinterpret_cast<Clip*>(&storage);
	selection.select(clip);
	{
		Scope remote(Id::Remote);
		selection.select(clip);
		selection.select(nullptr);
		POINTERS_EQUAL(nullptr, selection.current());
		POINTERS_EQUAL(clip, selection.previous());
	}
	POINTERS_EQUAL(clip, selection.current());
	selection.replace(clip, nullptr);
	{
		Scope remote(Id::Remote);
		POINTERS_EQUAL(nullptr, selection.previous());
	}
}

TEST(UISession, song_horizontal_navigation_preserves_both_panels_across_nested_scopes) {
	SongNavigation navigation;
	navigation.initialize(24, 96);
	navigation.active().xScroll[NAVIGATION_CLIP] = 48;
	navigation.active().xScroll[NAVIGATION_ARRANGEMENT] = -96;
	navigation.active().xScrollForReturnToSongView = 72;
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(0, navigation.active().xScroll[NAVIGATION_CLIP]);
		LONGS_EQUAL(24, navigation.active().xZoom[NAVIGATION_CLIP]);
		LONGS_EQUAL(96, navigation.active().xZoom[NAVIGATION_ARRANGEMENT]);
		LONGS_EQUAL(24, navigation.active().xZoomForReturnToSongView);
		navigation.active().xScroll[NAVIGATION_CLIP] = 192;
		navigation.active().xZoom[NAVIGATION_CLIP] = 12;
		navigation.active().xScrollForReturnToSongView = 384;
		navigation.active().xZoomForReturnToSongView = 48;
		{
			Scope local(Id::Local);
			LONGS_EQUAL(48, navigation.active().xScroll[NAVIGATION_CLIP]);
			LONGS_EQUAL(-96, navigation.active().xScroll[NAVIGATION_ARRANGEMENT]);
			LONGS_EQUAL(72, navigation.active().xScrollForReturnToSongView);
			LONGS_EQUAL(24, navigation.active().xZoomForReturnToSongView);
		}
		LONGS_EQUAL(192, navigation.active().xScroll[NAVIGATION_CLIP]);
		LONGS_EQUAL(12, navigation.active().xZoom[NAVIGATION_CLIP]);
		LONGS_EQUAL(384, navigation.active().xScrollForReturnToSongView);
		LONGS_EQUAL(48, navigation.active().xZoomForReturnToSongView);
	}
	LONGS_EQUAL(24, navigation.active().xZoom[NAVIGATION_CLIP]);
}

TEST(UISession, song_navigation_initialization_resets_both_banks_regardless_of_caller) {
	SongNavigation navigation;
	navigation.active().xScroll[NAVIGATION_CLIP] = 100;
	{
		Scope remote(Id::Remote);
		navigation.active().xScroll[NAVIGATION_ARRANGEMENT] = 200;
		navigation.initialize(6, 48);
		LONGS_EQUAL(0, navigation.active().xScroll[NAVIGATION_ARRANGEMENT]);
		LONGS_EQUAL(6, navigation.active().xZoomForReturnToSongView);
	}
	LONGS_EQUAL(0, navigation.active().xScroll[NAVIGATION_CLIP]);
	LONGS_EQUAL(6, navigation.active().xZoom[NAVIGATION_CLIP]);
	LONGS_EQUAL(48, navigation.active().xZoom[NAVIGATION_ARRANGEMENT]);
}

TEST(UISession, song_vertical_navigation_and_structural_changes_use_each_panels_viewport) {
	SongNavigation navigation;
	LONGS_EQUAL(1 - kDisplayHeight, navigation.active().songViewYScroll);
	navigation.active().songViewYScroll = 3;
	navigation.active().arrangementYScroll = 0;
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(1 - kDisplayHeight, navigation.active().songViewYScroll);
		navigation.active().songViewYScroll = 9;
		navigation.active().arrangementYScroll = -4;
	}
	// With different visible rows, deleting the same output moves only Local.
	navigation.output_removed(2, 7);
	LONGS_EQUAL(-1, navigation.active().arrangementYScroll);
	navigation.output_inserted_at_start();
	LONGS_EQUAL(0, navigation.active().arrangementYScroll);
	LONGS_EQUAL(3, navigation.active().songViewYScroll);
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(-3, navigation.active().arrangementYScroll);
		LONGS_EQUAL(9, navigation.active().songViewYScroll);
		navigation.initialize(12, 48);
		LONGS_EQUAL(-kDisplayHeight, navigation.active().arrangementYScroll);
	}
	LONGS_EQUAL(1 - kDisplayHeight, navigation.active().songViewYScroll);
}

TEST(UISession, session_clip_deletion_balances_each_viewport_independently) {
	SongNavigation navigation;
	navigation.active().songViewYScroll = 0;
	{
		Scope remote(Id::Remote);
		navigation.active().songViewYScroll = -4;
	}
	navigation.session_clip_removed(2, 7, false);
	LONGS_EQUAL(-1, navigation.active().songViewYScroll);
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(-4, navigation.active().songViewYScroll);
	}
}

TEST(UISession, forced_session_clip_movement_belongs_to_the_initiating_panel) {
	SongNavigation navigation;
	navigation.active().songViewYScroll = -4;
	{
		Scope remote(Id::Remote);
		navigation.active().songViewYScroll = -4;
		navigation.session_clip_removed(2, 7, true);
		LONGS_EQUAL(-5, navigation.active().songViewYScroll);
	}
	LONGS_EQUAL(-4, navigation.active().songViewYScroll);
}

TEST(UISession, pending_overdub_insertion_uses_each_panels_anchor) {
	SongNavigation navigation;
	navigation.active().songViewYScroll = 2;
	{
		Scope remote(Id::Remote);
		navigation.active().songViewYScroll = 5;
		navigation.pending_overdub_inserted(2);
		LONGS_EQUAL(6, navigation.active().songViewYScroll);
	}
	LONGS_EQUAL(2, navigation.active().songViewYScroll);
}

TEST(UISession, peer_clip_insertion_and_undo_keep_the_visible_anchor_without_moving_the_caller) {
	SongNavigation navigation;
	navigation.active().songViewYScroll = 3;
	{
		Scope remote(Id::Remote);
		navigation.active().songViewYScroll = 5;
	}
	navigation.peer_clip_inserted(2);
	LONGS_EQUAL(3, navigation.active().songViewYScroll);
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(6, navigation.active().songViewYScroll);
	}
	navigation.peer_clip_removed(2);
	navigation.peer_clip_inserted(8); // Beyond the anchor: keep its row.
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(5, navigation.active().songViewYScroll);
		navigation.peer_clip_inserted(3); // Also works for a Remote initiator.
	}
	LONGS_EQUAL(4, navigation.active().songViewYScroll);
}

TEST(UISession, peer_reordering_follows_the_anchor_and_deletion_of_it_keeps_the_row) {
	SongNavigation navigation;
	{
		Scope remote(Id::Remote);
		navigation.active().songViewYScroll = 5;
	}
	navigation.peer_clips_swapped(5, 6);
	navigation.peer_clip_removed(6);
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(6, navigation.active().songViewYScroll);
	}
	navigation.peer_clips_swapped(6, 5);
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(5, navigation.active().songViewYScroll);
	}
}

TEST(UISession, structural_refresh_waits_for_idle_overview_and_coalesces_edits) {
	StructuralRefresh refresh;
	refresh.request();
	refresh.request();
	CHECK_FALSE(refresh.consume(true, true, true));
	CHECK_FALSE(refresh.consume(false, false, true));
	CHECK_FALSE(refresh.consume(false, true, false));
	CHECK_TRUE(refresh.consume(false, true, true));
	CHECK_FALSE(refresh.consume(false, true, true));
	refresh.request();
	CHECK_TRUE(refresh.consume(false, true, true));
}

TEST(UISession, structural_refresh_notifies_only_the_other_panel) {
	navigation.for_owner(Id::Local).structural_refresh = {};
	navigation.for_owner(Id::Remote).structural_refresh = {};
	request_peer_structural_refresh();
	CHECK_FALSE(navigation.active().structural_refresh.consume(false, true, true));
	{
		Scope remote(Id::Remote);
		CHECK_TRUE(navigation.active().structural_refresh.consume(false, true, true));
		request_peer_structural_refresh();
		CHECK_FALSE(navigation.active().structural_refresh.consume(false, true, true));
	}
	CHECK_TRUE(navigation.active().structural_refresh.consume(false, true, true));
}

TEST(UISession, structural_revision_invalidates_holds_even_after_redraw_consumption) {
	StructuralRefresh refresh;
	const auto held_at = refresh.revision();
	refresh.request();
	CHECK(refresh.revision() != held_at);
	const auto first_edit = refresh.revision();
	CHECK_TRUE(refresh.consume(false, true, true));
	CHECK(refresh.revision() != held_at);
	refresh.request();
	CHECK(refresh.revision() != first_edit);
	const auto new_hold = refresh.revision();
	CHECK_FALSE(refresh.consume(true, true, true));
	CHECK_EQUAL(new_hold, refresh.revision());
}

TEST(UISession, grid_and_arrangement_return_navigation_stay_with_their_panel) {
	SongNavigation navigation;
	navigation.active().songGridScrollX = 4;
	navigation.active().songGridScrollY = 3;
	navigation.active().lastClipInstanceEnteredStartPos = 192;
	navigation.active().arrangerAutoScrollModeActive = true;
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(0, navigation.active().songGridScrollX);
		LONGS_EQUAL(0, navigation.active().songGridScrollY);
		LONGS_EQUAL(-1, navigation.active().lastClipInstanceEnteredStartPos);
		CHECK_FALSE(navigation.active().arrangerAutoScrollModeActive);
		navigation.active().songGridScrollX = 7;
		navigation.active().songGridScrollY = 1;
		navigation.active().lastClipInstanceEnteredStartPos = 0;
	}
	LONGS_EQUAL(4, navigation.active().songGridScrollX);
	LONGS_EQUAL(3, navigation.active().songGridScrollY);
	LONGS_EQUAL(192, navigation.active().lastClipInstanceEnteredStartPos);
	CHECK_TRUE(navigation.active().arrangerAutoScrollModeActive);
	navigation.initialize(12, 48);
	LONGS_EQUAL(0, navigation.active().songGridScrollX);
	LONGS_EQUAL(-1, navigation.active().lastClipInstanceEnteredStartPos);
	CHECK_FALSE(navigation.active().arrangerAutoScrollModeActive);
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(0, navigation.active().songGridScrollX);
		LONGS_EQUAL(0, navigation.active().songGridScrollY);
		LONGS_EQUAL(-1, navigation.active().lastClipInstanceEnteredStartPos);
	}
}

TEST(UISession, clip_screen_choice_and_clone_destination_are_panel_owned) {
	State<ClipViewState> source, clone;
	source.active().onKeyboardScreen = true;
	clone.active() = source.active();
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(source.active().onKeyboardScreen);
		CHECK_FALSE(clone.active().onKeyboardScreen);
		source.active().onAutomationClipView = true;
		clone.active() = source.active();
		CHECK_TRUE(clone.active().onAutomationClipView);
		CHECK_FALSE(clone.active().onKeyboardScreen);
	}
	CHECK_TRUE(source.active().onKeyboardScreen);
	CHECK_FALSE(source.active().onAutomationClipView);
	CHECK_TRUE(clone.active().onKeyboardScreen);
	CHECK_FALSE(clone.active().onAutomationClipView);
}

TEST(UISession, clip_pitch_and_drum_scrolling_remain_independent_when_cloned) {
	State<ClipViewState> clip, clone;
	clip.active().yScroll = 60;
	clone.active().yScroll = clip.active().yScroll;
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(0, clip.active().yScroll);
		clip.active().yScroll = -3;
		clone.active().yScroll = clip.active().yScroll;
		LONGS_EQUAL(-3, clone.active().yScroll);
	}
	LONGS_EQUAL(60, clip.active().yScroll);
	LONGS_EQUAL(60, clone.active().yScroll);
}

TEST(UISession, shared_clip_row_movement_preserves_both_viewports_and_their_distance) {
	State<ClipViewState> clip;
	clip.active().yScroll = 60;
	{
		Scope remote(Id::Remote);
		clip.active().yScroll = 72;
		shift_clip_views(clip, -12);
		LONGS_EQUAL(60, clip.active().yScroll);
	}
	LONGS_EQUAL(48, clip.active().yScroll);
	shift_clip_views(clip, 1);
	LONGS_EQUAL(49, clip.active().yScroll);
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(61, clip.active().yScroll);
	}
}

TEST(UISession, automation_selection_is_independent_for_clip_and_arranger) {
	State<ClipViewState> clip;
	SongNavigation song;
	clip.active().automation.lastSelectedParamID = 10;
	song.active().automation.lastSelectedParamID = 20;
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(static_cast<int32_t>(deluge::modulation::params::kNoParamID),
		            clip.active().automation.lastSelectedParamID);
		LONGS_EQUAL(static_cast<int32_t>(deluge::modulation::params::kNoParamID),
		            song.active().automation.lastSelectedParamID);
		clip.active().automation.lastSelectedParamID = 30;
		clip.active().automation.lastSelectedParamShortcutX = 4;
		song.active().automation.lastSelectedParamID = 40;
	}
	LONGS_EQUAL(10, clip.active().automation.lastSelectedParamID);
	LONGS_EQUAL(kNoSelection, clip.active().automation.lastSelectedParamShortcutX);
	LONGS_EQUAL(20, song.active().automation.lastSelectedParamID);
	{
		Scope remote(Id::Remote);
		LONGS_EQUAL(30, clip.active().automation.lastSelectedParamID);
		LONGS_EQUAL(40, song.active().automation.lastSelectedParamID);
	}
}

TEST(UISession, triplet_grid_selection_is_independent_and_resets_for_both_panels) {
	SongNavigation song;
	song.active().tripletsOn = true;
	song.active().tripletsLevel = 24;
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(song.active().tripletsOn);
		song.active().tripletsLevel = 12;
		song.active().tripletsOn = true;
	}
	CHECK_TRUE(song.active().tripletsOn);
	LONGS_EQUAL(24, song.active().tripletsLevel);
	song.active().tripletsOn = false;
	{
		Scope remote(Id::Remote);
		CHECK_TRUE(song.active().tripletsOn);
		LONGS_EQUAL(12, song.active().tripletsLevel);
	}
	song.initialize(12, 48);
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(song.active().tripletsOn);
	}
	CHECK_FALSE(song.active().tripletsOn);
}

TEST(UISession, recording_target_rejects_changed_identity_or_revision) {
	int song, sound, source, range, replacement;
	RecordingTarget target{&song, &sound, &source, &range, 5};
	CHECK(target == target);
	auto changed = target;
	changed.range = &replacement;
	CHECK_FALSE(changed == target);
	changed = target;
	++changed.revision;
	CHECK_FALSE(changed == target);
	changed = target;
	changed.song = &replacement;
	CHECK_FALSE(changed == target);
}

TEST(UISession, deferred_command_preserves_owner_and_rejects_recursive_service) {
	DeferredSessionCommand<int> command;
	{
		Scope remote(Id::Remote);
		command.pend(1);
	}
	int executions = 0;
	command.service([&](int value) {
		LONGS_EQUAL(1, value);
		CHECK(current() == Id::Remote);
		++executions;
		Scope local(Id::Local);
		command.pend(2);
		command.service([&](int) { FAIL("Recursive command execution"); });
	});
	CHECK(current() == Id::Local);
	LONGS_EQUAL(1, executions);
	{
		Scope remote(Id::Remote);
		command.service([&](int value) {
			LONGS_EQUAL(2, value);
			CHECK(current() == Id::Local);
			++executions;
		});
		CHECK(current() == Id::Remote);
	}
	command.service([&](int) { FAIL("Command executed twice"); });
	LONGS_EQUAL(2, executions);
}

TEST(UISession, deferred_command_keeps_latest_pending_command_and_owner) {
	DeferredSessionCommand<int> command;
	command.pend(1);
	{
		Scope remote(Id::Remote);
		command.pend(2);
	}
	command.service([](int value) {
		LONGS_EQUAL(2, value);
		CHECK(current() == Id::Remote);
	});
	CHECK(current() == Id::Local);
}

TEST(UISession, history_reset_cancels_deferred_command_without_changing_owner) {
	DeferredSessionCommand<int> command;
	{
		Scope remote(Id::Remote);
		command.pend(1);
	}
	command.cancel();
	CHECK(current() == Id::Local);
	command.service([](int) { FAIL("Cancelled command executed"); });
	command.pend(2);
	int executions = 0;
	command.service([&](int value) {
		LONGS_EQUAL(2, value);
		++executions;
	});
	LONGS_EQUAL(1, executions);
}

TEST(UISession, cancellation_during_execution_preserves_recursion_guard) {
	DeferredSessionCommand<int> command;
	command.pend(1);
	command.service([&](int) {
		command.pend(2);
		command.cancel();
		command.pend(3);
		command.service([](int) { FAIL("Cancellation released execution guard"); });
	});
	int executions = 0;
	command.service([&](int value) {
		LONGS_EQUAL(3, value);
		++executions;
	});
	LONGS_EQUAL(1, executions);
}

TEST(UISession, history_cleanup_rejects_requests_until_outermost_suspension_ends) {
	DeferredSessionCommand<int> command;
	command.pend(1);
	{
		auto outer = command.suspend();
		command.service([](int) { FAIL("Old history command survived suspension"); });
		{
			auto inner = command.suspend();
			Scope remote(Id::Remote);
			command.pend(2);
		}
		command.pend(3);
		command.service([](int) { FAIL("Nested cleanup resumed commands too early"); });
	}
	command.service([](int) { FAIL("Command queued during cleanup survived"); });
	command.pend(4);
	int executions = 0;
	command.service([&](int value) {
		LONGS_EQUAL(4, value);
		++executions;
	});
	LONGS_EQUAL(1, executions);
}

TEST(UISession, history_cleanup_inside_callback_preserves_execution_guard) {
	DeferredSessionCommand<int> command;
	command.pend(1);
	command.service([&](int) {
		{
			auto cleanup = command.suspend();
			command.pend(2);
		}
		command.pend(3);
		command.service([](int) { FAIL("Cleanup released active callback guard"); });
	});
	int executions = 0;
	command.service([&](int value) {
		LONGS_EQUAL(3, value);
		++executions;
	});
	LONGS_EQUAL(1, executions);
}

TEST(UISession, nested_reversion_cannot_release_active_history_guard) {
	bool active = false;
	{
		ReversionGuard outer(active);
		CHECK(static_cast<bool>(outer));
		CHECK(active);
		{
			ReversionGuard nested(active);
			CHECK_FALSE(static_cast<bool>(nested));
		}
		CHECK(active);
	}
	CHECK_FALSE(active);
	ReversionGuard next(active);
	CHECK(static_cast<bool>(next));
}

TEST(UISession, early_return_releases_history_guard) {
	bool active = false;
	auto attempt = [&]() {
		ReversionGuard guard(active);
		CHECK(static_cast<bool>(guard));
		return;
	};
	attempt();
	CHECK_FALSE(active);
}

TEST(UISession, history_cleanup_preserves_outer_reversion_across_both_queues) {
	bool active = false;
	{
		ReversionGuard full_cleanup(active);
		for (int queue = 0; queue < 2; ++queue) {
			{
				ReversionGuard queue_cleanup(active);
				ReversionGuard attempted_undo(active);
				CHECK_FALSE(static_cast<bool>(attempted_undo));
			}
			CHECK(active);
		}
		ReversionGuard attempted_redo(active);
		CHECK_FALSE(static_cast<bool>(attempted_redo));
	}
	CHECK_FALSE(active);
}

TEST(UISession, inspecting_remote_instance_does_not_construct_it) {
	OwnedEditor local;
	RemoteInstance<OwnedEditor> remote;
	POINTERS_EQUAL(&local, remote.get_if_initialized(local));
	{
		Scope owner(Id::Remote);
		POINTERS_EQUAL(nullptr, remote.get_if_initialized(local));
		auto& editor = remote.get(local);
		CHECK(editor.constructed_for == Id::Remote);
		POINTERS_EQUAL(&editor, remote.get_if_initialized(local));
	}
	POINTERS_EQUAL(&local, remote.get_if_initialized(local));
}

TEST(UISession, history_lookup_skips_peers_but_stops_at_newest_owned_action) {
	struct Entry {
		Id navigation_owner;
		Entry* nextAction;
	};
	Entry older{Id::Local, nullptr};
	Entry newest{Id::Local, &older};
	Entry peer{Id::Remote, &newest};
	POINTERS_EQUAL(&newest, newest_action_for_panel(&peer, Id::Local));
	POINTERS_EQUAL(&peer, newest_action_for_panel(&peer, Id::Remote));
	POINTERS_EQUAL(nullptr, newest_action_for_panel(&older, Id::Remote));
	POINTERS_EQUAL(nullptr, newest_action_for_panel(static_cast<Entry*>(nullptr), Id::Local));
	POINTERS_EQUAL(&newest, peer.nextAction);
}

TEST(UISession, clip_snapshot_defaults_are_safe_for_non_instrument_clips) {
	static_assert(std::is_trivially_destructible_v<ActionClipState>);
	static_assert(!std::is_polymorphic_v<ActionClipState>);
	ActionClipState state{};
	POINTERS_EQUAL(nullptr, state.clip_identity);
	POINTERS_EQUAL(nullptr, state.selected_drum_identity);
	LONGS_EQUAL(0, state.yScrollSessionView[0]);
	LONGS_EQUAL(0, state.yScrollSessionView[1]);
	CHECK_FALSE(state.affectEntire);
	CHECK_FALSE(state.wrapEditing);
	LONGS_EQUAL(0, state.wrapEditLevel);
}

TEST(UISession, clip_snapshot_rejects_replaced_output_without_dereferencing_saved_identities) {
	int clip_token, other_clip_token, output_token, other_output_token;
	auto* clip = reinterpret_cast<Clip*>(&clip_token);
	auto* output = reinterpret_cast<Output*>(&output_token);
	ActionClipState state{};
	POINTERS_EQUAL(nullptr, state.output_identity);
	state.clip_identity = clip;
	state.output_identity = output;
	CHECK(state.matches(clip, output));
	CHECK_FALSE(state.matches(clip, reinterpret_cast<Output*>(&other_output_token)));
	CHECK_FALSE(state.matches(reinterpret_cast<Clip*>(&other_clip_token), output));
	CHECK_FALSE(state.matches(nullptr, nullptr));
}

TEST(UISession, structural_change_exit_invalidates_the_captured_peer) {
	const auto local_before = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_before = navigation.for_owner(Id::Remote).structural_refresh.revision();
	{
		PeerStructuralChange change;
		CHECK(navigation.for_owner(Id::Remote).structural_refresh.revision() == remote_before + 1);
		Scope remote(Id::Remote);
		// change is destroyed after this scope; separately exercise exit under another owner below.
	}
	CHECK(navigation.for_owner(Id::Remote).structural_refresh.revision() == remote_before + 2);
	CHECK(navigation.for_owner(Id::Local).structural_refresh.revision() == local_before);
	{
		auto change = std::make_unique<PeerStructuralChange>();
		Scope remote(Id::Remote);
		change.reset();
	}
	CHECK(navigation.for_owner(Id::Remote).structural_refresh.revision() == remote_before + 4);
	CHECK(navigation.for_owner(Id::Local).structural_refresh.revision() == local_before);
}

TEST(UISession, empty_structural_change_does_not_invalidate_either_panel) {
	const auto local_before = navigation.for_owner(Id::Local).structural_refresh.revision();
	const auto remote_before = navigation.for_owner(Id::Remote).structural_refresh.revision();
	{ PeerStructuralChange change(false); }
	CHECK(navigation.for_owner(Id::Local).structural_refresh.revision() == local_before);
	CHECK(navigation.for_owner(Id::Remote).structural_refresh.revision() == remote_before);
}

TEST(UISession, clip_edit_scope_and_wrap_settings_remain_panel_owned) {
	State<ClipViewState> clip, clone;
	clip.active().wrapEditing = true;
	clip.active().wrapEditLevel = 96;
	clip.active().affectEntire = false;
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(clip.active().wrapEditing);
		CHECK_TRUE(clip.active().affectEntire);
		clip.active().wrapEditLevel = 192;
		clone.active() = clip.active();
		LONGS_EQUAL(192, clone.active().wrapEditLevel);
	}
	CHECK_TRUE(clip.active().wrapEditing);
	CHECK_FALSE(clip.active().affectEntire);
	LONGS_EQUAL(96, clip.active().wrapEditLevel);
	LONGS_EQUAL(0, clone.active().wrapEditLevel);
	reset_clip_affect_entire(clip, true);
	CHECK_TRUE(clip.active().affectEntire);
	{
		Scope remote(Id::Remote);
		CHECK_TRUE(clip.active().affectEntire);
		reset_clip_affect_entire(clip, false);
		CHECK_FALSE(clip.active().affectEntire);
		LONGS_EQUAL(192, clip.active().wrapEditLevel);
	}
	CHECK_FALSE(clip.active().affectEntire);
	LONGS_EQUAL(96, clip.active().wrapEditLevel);
}

TEST(UISession, song_layout_and_edit_scope_use_independent_panel_selections) {
	SongNavigation song;
	song.initialize_layout(SessionLayoutType::SessionLayoutTypeGrid);
	CHECK_TRUE(song.active().sessionLayout == SessionLayoutType::SessionLayoutTypeGrid);
	song.active().affectEntire = true;
	{
		Scope remote(Id::Remote);
		CHECK_TRUE(song.active().sessionLayout == SessionLayoutType::SessionLayoutTypeGrid);
		CHECK_FALSE(song.active().affectEntire);
		song.active().sessionLayout = SessionLayoutType::SessionLayoutTypeRows;
	}
	CHECK_TRUE(song.active().sessionLayout == SessionLayoutType::SessionLayoutTypeGrid);
	CHECK_TRUE(song.active().affectEntire);
}

#include "model/action/reversible_prefix.h"

TEST(UISession, reversible_prefix_rolls_back_every_failure_position_and_preserves_history) {
	struct Node {
		Node* next;
		int snapshot;
	};
	for (int fail_at = 0; fail_at < 3; ++fail_at) {
		Node suffix{nullptr, 40};
		Node third{&suffix, 30}, second{&third, 20}, first{&second, 10};
		Node* head = &first;
		Node* retired = nullptr;
		int live = 99, calls = 0, rollbacks = 0;
		CHECK_TRUE(PrefixResult::ROLLED_BACK
		           == apply_reversible_prefix(
		               head, &suffix, retired,
		               [&](Node& node) {
			               if (calls++ == fail_at)
				               return false;
			               std::swap(live, node.snapshot);
			               return true;
		               },
		               [&](Node& node) {
			               ++rollbacks;
			               std::swap(live, node.snapshot);
			               return true;
		               }));
		LONGS_EQUAL(99, live);
		LONGS_EQUAL(10, first.snapshot);
		LONGS_EQUAL(20, second.snapshot);
		LONGS_EQUAL(30, third.snapshot);
		LONGS_EQUAL(40, suffix.snapshot);
		LONGS_EQUAL(fail_at, rollbacks);
		POINTERS_EQUAL(&first, head);
		POINTERS_EQUAL(&second, first.next);
		POINTERS_EQUAL(&third, second.next);
		POINTERS_EQUAL(&suffix, third.next);
		POINTERS_EQUAL(nullptr, retired);
	}
}

TEST(UISession, reversible_prefix_detaches_snapshots_only_after_all_swaps_succeed) {
	struct Node {
		Node* next;
		int snapshot;
	};
	Node suffix{nullptr, 30}, second{&suffix, 20}, first{&second, 10};
	Node* head = &first;
	Node* retired = nullptr;
	int live = 99;
	CHECK_TRUE(PrefixResult::APPLIED
	           == apply_reversible_prefix(
	               head, &suffix, retired,
	               [&](Node& node) {
		               std::swap(live, node.snapshot);
		               return true;
	               },
	               [&](Node&) {
		               FAIL("Successful prefix must not roll back");
		               return false;
	               }));
	LONGS_EQUAL(20, live);
	LONGS_EQUAL(99, first.snapshot);
	LONGS_EQUAL(10, second.snapshot);
	LONGS_EQUAL(30, suffix.snapshot);
	POINTERS_EQUAL(&suffix, head);
	POINTERS_EQUAL(&second, retired);
	POINTERS_EQUAL(&first, retired->next);
	POINTERS_EQUAL(nullptr, first.next);
}

#include "util/container/retained_list.h"

TEST(UISession, undo_detached_objects_keep_one_owner_across_restore_and_discard) {
	struct Output {
		Output* next = nullptr;
		int id;
	};
	Output first{nullptr, 1}, second{nullptr, 2}, third{nullptr, 3};
	RetainedList<Output> song;
	song.retain(&first);
	song.retain(&second);
	song.retain(&third);
	// Repeated removal notifications must not create an intrusive-list cycle.
	song.retain(&second);
	POINTERS_EQUAL(&third, song.head());
	POINTERS_EQUAL(&second, third.next);
	POINTERS_EQUAL(&first, second.next);
	POINTERS_EQUAL(nullptr, first.next);
	// Restore the middle entry without disturbing the other pending histories.
	CHECK_TRUE(song.release(&second));
	POINTERS_EQUAL(&first, third.next);
	POINTERS_EQUAL(nullptr, second.next);
	CHECK_FALSE(song.release(&second));
	CHECK_FALSE(song.contains(&second));
	// A later undo can retain it again. Discarding history does not free objects
	// still referenced by the model; song teardown drains each object once.
	song.retain(&second);
	int visited = 0, mask = 0;
	while (song.head()) {
		auto* output = song.head();
		CHECK_TRUE(song.release(output));
		CHECK_FALSE(mask & (1 << output->id));
		mask |= 1 << output->id;
		++visited;
	}
	LONGS_EQUAL(3, visited);
	LONGS_EQUAL(14, mask);
	CHECK_FALSE(song.release(nullptr));
}

#include "util/container/array/reserved_ring_insert.h"

TEST(UISession, reserved_ring_insertion_preserves_logical_order_across_every_wrap) {
	for (int capacity = 1; capacity <= 12; ++capacity) {
		for (int start = 0; start < capacity; ++start) {
			for (int count = 0; count < capacity; ++count) {
				for (int index = 0; index <= count; ++index) {
					for (int amount = 1; amount <= capacity - count; ++amount) {
						std::array<int, 16> storage;
						storage.fill(-999);
						auto* ring = storage.data() + 1;
						for (int i = 0; i < count; ++i)
							ring[(start + i) % capacity] = 100 + i;
						int32_t new_count = count;
						CHECK_TRUE(
						    insert_into_reserved_ring(ring, capacity, start, new_count, sizeof(int), index, amount));
						LONGS_EQUAL(count + amount, new_count);
						for (int i = index; i < index + amount; ++i)
							ring[(start + i) % capacity] = -1;
						for (int i = 0; i < new_count; ++i) {
							int expected = i < index ? 100 + i : i < index + amount ? -1 : 100 + i - amount;
							LONGS_EQUAL(expected, ring[(start + i) % capacity]);
						}
						LONGS_EQUAL(-999, storage.front());
						LONGS_EQUAL(-999, storage[capacity + 1]);
					}
				}
			}
		}
	}
}

TEST(UISession, reserved_ring_rejects_insufficient_capacity_and_invalid_ranges_without_mutation) {
	std::array<int, 4> storage{10, 20, 30, 40};
	const auto original = storage;
	int32_t count = 4;
	CHECK_FALSE(insert_into_reserved_ring(storage.data(), 4, 0, count, sizeof(int), 2, 1));
	LONGS_EQUAL(4, count);
	count = 2;
	CHECK_FALSE(insert_into_reserved_ring(storage.data(), 4, 0, count, sizeof(int), -1, 1));
	CHECK_FALSE(insert_into_reserved_ring(storage.data(), 4, 0, count, sizeof(int), 3, 1));
	CHECK_FALSE(insert_into_reserved_ring(storage.data(), 4, 0, count, sizeof(int), 0, 0));
	CHECK_FALSE(insert_into_reserved_ring(storage.data(), 4, 4, count, sizeof(int), 0, 1));
	CHECK_FALSE(insert_into_reserved_ring(storage.data(), 4, 0, count, 0, 0, 1));
	CHECK_FALSE(insert_into_reserved_ring(nullptr, 4, 0, count, sizeof(int), 0, 1));
	LONGS_EQUAL(2, count);
	MEMCMP_EQUAL(original.data(), storage.data(), sizeof(storage));
}

#include "hid/encoder_input_bank.h"

TEST(UISession, injected_encoder_dispatch_stays_occupied_across_yield_and_retry) {
	using namespace deluge::hid::encoders;
	State<EncoderInputBank> panels;
	auto& local = panels.active();
	CHECK_TRUE(local.queue(1, 9));
	{
		EncoderInputBank::Dispatch dispatch(local);
		CHECK_TRUE(static_cast<bool>(dispatch));
		int32_t delta = local.functions[1].take();
		CHECK_TRUE(local.pending());
		CHECK_FALSE(local.queue(2, 1));
		{
			EncoderInputBank::Dispatch reentrant(local);
			CHECK_FALSE(static_cast<bool>(reentrant));
		}
		CHECK_TRUE(local.pending());
		{
			Scope remote(Id::Remote);
			CHECK_TRUE(panels.active().queue(0, -2));
			EncoderInputBank::Dispatch remote_dispatch(panels.active());
			CHECK_TRUE(static_cast<bool>(remote_dispatch));
			LONGS_EQUAL(-2, panels.active().functions[0].take());
		}
		local.functions[1].restore(delta);
	}
	CHECK_TRUE(local.pending());
	CHECK_FALSE(local.queue(2, 1));
	{
		EncoderInputBank::Dispatch retry(local);
		CHECK_TRUE(static_cast<bool>(retry));
		LONGS_EQUAL(9, local.functions[1].take());
	}
	CHECK_FALSE(local.pending());
	CHECK_TRUE(local.queue(2, 1));
}

TEST(UISession, injected_encoder_clear_during_dispatch_discards_restored_retry) {
	using namespace deluge::hid::encoders;
	EncoderInputBank bank;
	CHECK_TRUE(bank.queue(0, 3));
	{
		EncoderInputBank::Dispatch dispatch(bank);
		int32_t delta = bank.functions[0].take();
		bank.clear();
		bank.clear();
		CHECK_TRUE(bank.pending());
		CHECK_FALSE(bank.queue(1, 1));
		bank.functions[0].restore(delta);
	}
	CHECK_FALSE(bank.pending());
	LONGS_EQUAL(0, bank.functions[0].take());
	CHECK_TRUE(bank.queue(0, 4));
	{
		EncoderInputBank::Dispatch dispatch(bank);
		LONGS_EQUAL(4, bank.functions[0].take());
		bank.functions[0].restore(4);
	}
	CHECK_TRUE(bank.pending());
	bank.clear();
	CHECK_FALSE(bank.pending());
}

TEST(UISession, injected_encoder_banks_preserve_panel_ownership_and_physical_ticks) {
	using namespace deluge::hid::encoders;
	State<EncoderInputBank> panels;
	DetentedEncoder physical;
	physical.restore(7);
	CHECK_TRUE(panels.active().queue(0, 3));
	{
		Scope remote(Id::Remote);
		CHECK_FALSE(panels.active().pending());
		CHECK_TRUE(panels.active().queue(5, -4));
		CHECK_FALSE(panels.active().queue(0, 1));
		panels.active().clear();
		CHECK_FALSE(panels.active().pending());
	}
	CHECK_TRUE(panels.active().pending());
	LONGS_EQUAL(3, panels.active().functions[0].take());
	LONGS_EQUAL(7, physical.take());
	CHECK_FALSE(panels.active().pending());
}

TEST(UISession, injected_encoder_retry_cannot_be_overtaken_or_accumulate_another_input) {
	using namespace deluge::hid::encoders;
	EncoderInputBank bank;
	CHECK_TRUE(bank.queue(1, -127));
	int32_t deferred = bank.functions[1].take();
	bank.functions[1].restore(deferred);
	CHECK_FALSE(bank.queue(1, -127));
	CHECK_FALSE(bank.queue(4, 1));
	LONGS_EQUAL(-127, bank.functions[1].take());
	CHECK_TRUE(bank.queue(4, 127));
	LONGS_EQUAL(127, bank.mods[0].take());
	CHECK_FALSE(bank.pending());
	CHECK_FALSE(bank.queue(6, 1));
	CHECK_FALSE(bank.queue(255, 1));
	CHECK_FALSE(bank.queue(0, 0));
	CHECK_FALSE(bank.queue(0, 128));
	CHECK_FALSE(bank.queue(0, -128));
	for (uint8_t index = 0; index < 6; ++index) {
		for (int delta = -127; delta <= 127; ++delta) {
			if (!delta)
				continue;
			CHECK_TRUE(bank.queue(index, delta));
			int actual = index < 4 ? bank.functions[index].take() : bank.mods[index - 4].take();
			LONGS_EQUAL(delta, actual);
			CHECK_FALSE(bank.pending());
		}
	}
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

TEST(UISession, failed_prefix_rollback_stops_recovery_and_preserves_every_link) {
	struct Node {
		Node* next;
	};
	for (int fail_rollback_at : {0, 1}) {
		Node suffix{nullptr}, third{&suffix}, second{&third}, first{&second};
		Node* head = &first;
		Node* retired = nullptr;
		int applies = 0, rollbacks = 0;
		auto result = apply_reversible_prefix(
		    head, &suffix, retired, [&](Node&) { return applies++ < 2; },
		    [&](Node&) { return rollbacks++ != fail_rollback_at; });
		CHECK_TRUE(result == PrefixResult::ROLLBACK_FAILED);
		LONGS_EQUAL(fail_rollback_at + 1, rollbacks);
		POINTERS_EQUAL(&first, head);
		POINTERS_EQUAL(&second, first.next);
		POINTERS_EQUAL(&third, second.next);
		POINTERS_EQUAL(&suffix, third.next);
		POINTERS_EQUAL(nullptr, retired);
	}
}

TEST(UISession, remote_graphics_never_queries_physical_output_capacity) {
	Scope remote(Id::Remote);
	int queries = 0;
	CHECK_TRUE(graphics_output_ready([&] {
		++queries;
		return false;
	}));
	LONGS_EQUAL(0, queries);
}
TEST(UISession, local_graphics_preserves_physical_backpressure) {
	Scope local(Id::Local);
	int queries = 0;
	for (bool available : {false, true}) {
		CHECK_TRUE(graphics_output_ready([&] {
			           ++queries;
			           return available;
		           })
		           == available);
	}
	LONGS_EQUAL(2, queries);
}
TEST(UISession, graphics_capacity_routing_restores_after_nested_local_service) {
	Scope remote(Id::Remote);
	int queries = 0;
	auto full = [&] {
		++queries;
		return false;
	};
	CHECK_TRUE(graphics_output_ready(full));
	{
		Scope local(Id::Local);
		CHECK_FALSE(graphics_output_ready(full));
	}
	CHECK_TRUE(graphics_output_ready(full));
	LONGS_EQUAL(1, queries);
}
