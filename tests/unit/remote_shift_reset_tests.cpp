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
