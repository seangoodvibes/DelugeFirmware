#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_navigation_state.h"
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

namespace community_reset_test {
using sticky_setting_test::RuntimeFeatureSettingType;
namespace RuntimeFeatureStateToggle = sticky_setting_test::RuntimeFeatureStateToggle;
namespace l10n {
enum class String { STRING_FOR_RESET_COMMUNITY_FEATURES, STRING_FOR_FACTORY_RESET };
const char* get(String) {
	return "reset";
}
} // namespace l10n
struct display_fixture {
	bool haveOLED() { return true; }
	void displayPopup(const char*) {}
};
static display_fixture display_instance;
static auto* display = &display_instance;
constexpr const char* RUNTIME_FEATURE_SETTINGS_FILE = "settings";
static int unlink_calls = 0;
void f_unlink(const char*) {
	++unlink_calls;
}
struct reset_field {
	bool reset = false;
	void empty() { reset = true; }
	void clear() { reset = true; }
};
struct RuntimeFeatureSettings {
	reset_field unknownSettings, startupSong;
	int sticky = 1;
	int loaded_sticky = 0;
	int loads = 0;
	void init() { sticky = 0; }
	void readSettingsFromFile() {
		sticky = loaded_sticky;
		++loads;
	}
	int get(RuntimeFeatureSettingType) { return sticky; }
	void factoryReset(bool showPopup);
};
#include "community_settings_reset.inc"
} // namespace community_reset_test
TEST(RemoteShiftReset, settings_reset_clears_both_latches_and_requests_both_menu_refreshes) {
	namespace session = deluge::gui::ui_session;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		Buttons::button_states = {};
		for (auto panel : {session::Id::Local, session::Id::Remote}) {
			auto& state = Buttons::button_states.for_owner(panel);
			state.shiftCurrentlyPressed = state.shiftCurrentlyStuck = true;
			session::navigation.for_owner(panel).shared_model_refresh = {};
		}
		const auto shift = deluge::hid::button::toXY(deluge::hid::button::SHIFT);
		Buttons::button_states.for_owner(session::Id::Remote).buttonStates[shift.x][shift.y] = true;
		community_reset_test::RuntimeFeatureSettings settings;
		community_reset_test::unlink_calls = 0;
		settings.factoryReset(false);
		CHECK(session::current() == owner);
		LONGS_EQUAL(0, settings.sticky);
		LONGS_EQUAL(1, settings.loads);
		LONGS_EQUAL(1, community_reset_test::unlink_calls);
		CHECK(settings.unknownSettings.reset);
		CHECK(settings.startupSong.reset);
		for (auto panel : {session::Id::Local, session::Id::Remote}) {
			auto& state = Buttons::button_states.for_owner(panel);
			CHECK_FALSE(state.shiftCurrentlyStuck);
			CHECK_EQUAL(panel == session::Id::Remote, state.shiftCurrentlyPressed);
			CHECK_TRUE(state.shiftHasChangedSinceLastCheck);
			auto& refresh = session::navigation.for_owner(panel).shared_model_refresh;
			CHECK(refresh.consume(0));
			CHECK_FALSE(refresh.consume(0));
		}
	}
	Buttons::button_states = {};
}
TEST(RemoteShiftReset, settings_reset_uses_reloaded_sticky_setting_before_clearing_latches) {
	namespace session = deluge::gui::ui_session;
	session::Scope scope(session::Id::Remote);
	Buttons::button_states = {};
	for (auto panel : {session::Id::Local, session::Id::Remote}) {
		auto& state = Buttons::button_states.for_owner(panel);
		state.shiftCurrentlyPressed = state.shiftCurrentlyStuck = true;
		session::navigation.for_owner(panel).shared_model_refresh = {};
	}
	community_reset_test::RuntimeFeatureSettings settings;
	settings.loaded_sticky = 1;
	settings.factoryReset(true);
	LONGS_EQUAL(1, settings.sticky);
	CHECK(session::current() == session::Id::Remote);
	for (auto panel : {session::Id::Local, session::Id::Remote}) {
		auto& state = Buttons::button_states.for_owner(panel);
		CHECK(state.shiftCurrentlyStuck);
		CHECK(state.shiftCurrentlyPressed);
		CHECK_FALSE(state.shiftHasChangedSinceLastCheck);
		CHECK(session::navigation.for_owner(panel).shared_model_refresh.consume(0));
	}
	Buttons::button_states = {};
}
