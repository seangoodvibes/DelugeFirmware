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
