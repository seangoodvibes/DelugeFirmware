#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "hid/buttons.h"
namespace Buttons {
static deluge::gui::ui_session::State<State> button_states;
State& state() {
	return button_states.active();
}
#include "clear_shift_sticky.inc"
} // namespace Buttons
TEST_GROUP(RemoteShiftReset){};
TEST(RemoteShiftReset, production_clear_releases_remote_shift_and_notifies_without_changing_local) {
	namespace session = deluge::gui::ui_session;
	auto& local = Buttons::button_states.for_owner(session::Id::Local);
	auto& remote = Buttons::button_states.for_owner(session::Id::Remote);
	local = {};
	remote = {};
	local.shiftCurrentlyStuck = local.shiftCurrentlyPressed = true;
	remote.shiftCurrentlyStuck = remote.shiftCurrentlyPressed = true;
	{
		session::Scope owner(session::Id::Remote);
		Buttons::clearShiftSticky();
	}
	CHECK_FALSE(remote.shiftCurrentlyStuck);
	CHECK_FALSE(remote.shiftCurrentlyPressed);
	CHECK_TRUE(remote.shiftHasChangedSinceLastCheck);
	CHECK_TRUE(local.shiftCurrentlyStuck);
	CHECK_TRUE(local.shiftCurrentlyPressed);
	CHECK_FALSE(local.shiftHasChangedSinceLastCheck);
	local = {};
	remote = {};
}

TEST(RemoteShiftReset, startup_clears_all_remote_button_holds_and_release_actions_only) {
	namespace session = deluge::gui::ui_session;
	auto& local = Buttons::button_states.for_owner(session::Id::Local);
	auto& remote = Buttons::button_states.for_owner(session::Id::Remote);
	for (auto* panel : {&local, &remote}) {
		*panel = {};
		panel->recordButtonPressUsedUp = true;
		panel->considerCrossScreenReleaseForCrossScreenMode = true;
		panel->selectButtonPressUsedUp = true;
		panel->timeRecordButtonPressed = 101;
		panel->timeShiftButtonPressed = 202;
		panel->shiftCurrentlyPressed = panel->shiftCurrentlyStuck = true;
		panel->shift_led_enabled = true;
		panel->considerShiftReleaseForSticky = true;
		for (auto& column : panel->buttonStates)
			for (auto& pressed : column)
				pressed = true;
	}
	{
		session::Scope owner(session::Id::Remote);
		Buttons::reset_for_session_startup();
		Buttons::reset_for_session_startup();
	}
	CHECK_FALSE(remote.recordButtonPressUsedUp);
	CHECK_FALSE(remote.considerCrossScreenReleaseForCrossScreenMode);
	CHECK_FALSE(remote.selectButtonPressUsedUp);
	LONGS_EQUAL(0, remote.timeRecordButtonPressed);
	LONGS_EQUAL(0, remote.timeShiftButtonPressed);
	CHECK_FALSE(remote.shiftCurrentlyPressed);
	CHECK_FALSE(remote.shiftCurrentlyStuck);
	CHECK_FALSE(remote.considerShiftReleaseForSticky);
	CHECK_FALSE(remote.shift_led_enabled);
	CHECK_TRUE(remote.shiftHasChangedSinceLastCheck);
	for (auto& column : remote.buttonStates)
		for (auto pressed : column)
			CHECK_FALSE(pressed);
	CHECK_TRUE(local.recordButtonPressUsedUp);
	CHECK_TRUE(local.considerCrossScreenReleaseForCrossScreenMode);
	CHECK_TRUE(local.selectButtonPressUsedUp);
	LONGS_EQUAL(101, local.timeRecordButtonPressed);
	LONGS_EQUAL(202, local.timeShiftButtonPressed);
	CHECK_TRUE(local.shiftCurrentlyPressed);
	CHECK_TRUE(local.shiftCurrentlyStuck);
	CHECK_TRUE(local.considerShiftReleaseForSticky);
	CHECK_TRUE(local.shift_led_enabled);
	CHECK_FALSE(local.shiftHasChangedSinceLastCheck);
	for (auto& column : local.buttonStates)
		for (auto pressed : column)
			CHECK_TRUE(pressed);
	local = {};
	remote = {};
}

namespace sticky_setting_test {
namespace ui_session = deluge::gui::ui_session;
enum class RuntimeFeatureSettingType { ShiftIsSticky, LightShiftLed };
namespace RuntimeFeatureStateToggle {
constexpr int Off = 0, On = 1;
}
struct settings_fixture {
	int sticky = 1;
	int light = 0;
	int get(RuntimeFeatureSettingType type) {
		return type == RuntimeFeatureSettingType::ShiftIsSticky ? sticky : light;
	}
	void set(RuntimeFeatureSettingType type, int value) {
		(type == RuntimeFeatureSettingType::ShiftIsSticky ? sticky : light) = value;
	}
};
static settings_fixture runtimeFeatureSettings;
struct Setting {
	int value = 0;
	void writeCurrentValue() { runtimeFeatureSettings.sticky = value; }
};
struct ShiftIsSticky : Setting {
	void writeCurrentValue();
};
#include "sticky_setting_write.inc"
} // namespace sticky_setting_test
TEST(RemoteShiftReset, disabling_shared_sticky_setting_clears_both_panels_but_preserves_physical_hold) {
	namespace session = deluge::gui::ui_session;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& local = Buttons::button_states.for_owner(session::Id::Local);
		auto& remote = Buttons::button_states.for_owner(session::Id::Remote);
		local = remote = {};
		local.shiftCurrentlyStuck = local.shiftCurrentlyPressed = true;
		remote.shiftCurrentlyStuck = remote.shiftCurrentlyPressed = true;
		const auto shift = deluge::hid::button::toXY(deluge::hid::button::SHIFT);
		remote.buttonStates[shift.x][shift.y] = true;
		sticky_setting_test::runtimeFeatureSettings = {};
		sticky_setting_test::ShiftIsSticky menu;
		menu.value = 0;
		menu.writeCurrentValue();
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, sticky_setting_test::runtimeFeatureSettings.sticky);
		CHECK_FALSE(local.shiftCurrentlyStuck);
		CHECK_FALSE(remote.shiftCurrentlyStuck);
		CHECK_FALSE(local.shiftCurrentlyPressed);
		CHECK_TRUE(remote.shiftCurrentlyPressed);
		CHECK_TRUE(local.shiftHasChangedSinceLastCheck);
		CHECK_TRUE(remote.shiftHasChangedSinceLastCheck);
		CHECK_TRUE(remote.buttonStates[shift.x][shift.y]);
		local = remote = {};
	}
}
TEST(RemoteShiftReset, enabling_shared_sticky_setting_keeps_panel_holds_and_enables_led_setting) {
	namespace session = deluge::gui::ui_session;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& local = Buttons::button_states.for_owner(session::Id::Local);
		auto& remote = Buttons::button_states.for_owner(session::Id::Remote);
		local = remote = {};
		local.shiftCurrentlyPressed = true;
		remote.shiftCurrentlyPressed = remote.shiftCurrentlyStuck = true;
		sticky_setting_test::runtimeFeatureSettings = {};
		sticky_setting_test::ShiftIsSticky menu;
		menu.value = 1;
		menu.writeCurrentValue();
		CHECK(session::current() == owner);
		LONGS_EQUAL(1, sticky_setting_test::runtimeFeatureSettings.sticky);
		LONGS_EQUAL(1, sticky_setting_test::runtimeFeatureSettings.light);
		CHECK_TRUE(local.shiftCurrentlyPressed);
		CHECK_TRUE(remote.shiftCurrentlyPressed);
		CHECK_TRUE(remote.shiftCurrentlyStuck);
		CHECK_FALSE(local.shiftHasChangedSinceLastCheck);
		CHECK_FALSE(remote.shiftHasChangedSinceLastCheck);
		local = remote = {};
	}
}

namespace shift_feedback_test {
namespace session = deluge::gui::ui_session;
using sticky_setting_test::runtimeFeatureSettings;
using sticky_setting_test::RuntimeFeatureSettingType;
namespace RuntimeFeatureStateToggle = sticky_setting_test::RuntimeFeatureStateToggle;
namespace indicator_leds {
enum class LED { SHIFT };
static session::State<int> writes;
static session::State<bool> lit;
void setLedState(LED, bool value) {
	++writes.active();
	lit.active() = value;
}
} // namespace indicator_leds
namespace Buttons {
using ::Buttons::isShiftButtonPressed;
using ::Buttons::shiftHasChanged;
using ::Buttons::state;
#include "shift_led_feedback.inc"
} // namespace Buttons
} // namespace shift_feedback_test
TEST(RemoteShiftReset, shift_feedback_consumes_only_active_panel_change_and_emits_its_value) {
	namespace feedback = shift_feedback_test;
	namespace session = deluge::gui::ui_session;
	auto& local = Buttons::button_states.for_owner(session::Id::Local);
	auto& remote = Buttons::button_states.for_owner(session::Id::Remote);
	local = remote = {};
	local.shiftCurrentlyPressed = true;
	local.shiftHasChangedSinceLastCheck = remote.shiftHasChangedSinceLastCheck = true;
	feedback::runtimeFeatureSettings.light = 1;
	feedback::indicator_leds::writes = {};
	feedback::indicator_leds::lit = {};
	{
		session::Scope scope(session::Id::Remote);
		feedback::Buttons::update_shift_led();
		feedback::Buttons::update_shift_led();
		LONGS_EQUAL(1, feedback::indicator_leds::writes.active());
		CHECK_FALSE(feedback::indicator_leds::lit.active());
	}
	CHECK_TRUE(local.shiftHasChangedSinceLastCheck);
	CHECK_FALSE(remote.shiftHasChangedSinceLastCheck);
	LONGS_EQUAL(0, feedback::indicator_leds::writes.active());
	feedback::Buttons::update_shift_led();
	LONGS_EQUAL(1, feedback::indicator_leds::writes.active());
	CHECK_TRUE(feedback::indicator_leds::lit.active());
	CHECK_FALSE(local.shiftHasChangedSinceLastCheck);
	local = remote = {};
}
TEST(RemoteShiftReset, disabled_shift_feedback_does_not_write_either_panel_led) {
	namespace feedback = shift_feedback_test;
	namespace session = deluge::gui::ui_session;
	feedback::runtimeFeatureSettings.light = 0;
	feedback::indicator_leds::writes = {};
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		Buttons::state() = {};
		Buttons::state().shiftCurrentlyPressed = true;
		Buttons::state().shiftHasChangedSinceLastCheck = true;
		feedback::Buttons::update_shift_led();
		LONGS_EQUAL(0, feedback::indicator_leds::writes.active());
		CHECK_FALSE(Buttons::state().shiftHasChangedSinceLastCheck);
		Buttons::state() = {};
	}
}

TEST(RemoteShiftReset, shared_shift_led_setting_changes_apply_without_a_button_event) {
	namespace feedback = shift_feedback_test;
	namespace session = deluge::gui::ui_session;
	Buttons::button_states = {};
	feedback::indicator_leds::writes = {};
	feedback::indicator_leds::lit = {};
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		Buttons::state().shiftCurrentlyPressed = true;
	}
	feedback::runtimeFeatureSettings.light = 1;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		feedback::Buttons::update_shift_led();
		CHECK_TRUE(feedback::indicator_leds::lit.active());
		LONGS_EQUAL(1, feedback::indicator_leds::writes.active());
		feedback::Buttons::update_shift_led();
		LONGS_EQUAL(1, feedback::indicator_leds::writes.active());
	}
	feedback::runtimeFeatureSettings.light = 0;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		feedback::Buttons::update_shift_led();
		CHECK_FALSE(feedback::indicator_leds::lit.active());
		LONGS_EQUAL(2, feedback::indicator_leds::writes.active());
		CHECK_TRUE(Buttons::state().shiftCurrentlyPressed);
		feedback::Buttons::update_shift_led();
		LONGS_EQUAL(2, feedback::indicator_leds::writes.active());
	}
	Buttons::button_states = {};
}
