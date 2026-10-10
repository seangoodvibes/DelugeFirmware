#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/context_menu/clip_settings/launch_style.h"
namespace deluge::gui {
#include "context_menu_input.inc"
}
using deluge::gui::context_menu::clip_settings::LaunchStyleMenu;
TEST_GROUP(LaunchStyleMenu) {
	Clip clip;
	LaunchStyleMenu local_menu, remote_menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		session::navigation = {};
		redraws = {};
		text = {};
		modes = {};
		display_instance.oled = true;
		local_menu.clip = remote_menu.clip = &clip;
		local_menu.setupAndCheckAvailability();
		session::Scope owner(session::Id::Remote);
		remote_menu.setupAndCheckAvailability();
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(LaunchStyleMenu, second_panel_edits_from_live_clip_value_for_both_displays) {
	for (bool oled : {true, false}) {
		display_instance.oled = oled;
		clip.launchStyle = LaunchStyle::DEFAULT;
		local_menu.setupAndCheckAvailability();
		{
			session::Scope owner(session::Id::Remote);
			remote_menu.setupAndCheckAvailability();
			remote_menu.selectEncoderAction(1);
		}
		CHECK(clip.launchStyle == LaunchStyle::FILL);
		local_menu.selectEncoderAction(1);
		CHECK(clip.launchStyle == LaunchStyle::ONCE);
	}
}
TEST(LaunchStyleMenu, commit_defers_peer_refresh_and_preserves_its_navigation_mode) {
	local_menu.selectEncoderAction(1);
	LONGS_EQUAL(0, redraws.for_owner(session::Id::Remote));
	session::Scope owner(session::Id::Remote);
	currentUIMode = 123;
	CHECK(session::navigation.active().shared_model_refresh.consume(0));
	remote_menu.refresh_shared_model();
	LONGS_EQUAL(1, remote_menu.currentOption);
	LONGS_EQUAL(1, remote_menu.scrollPos);
	LONGS_EQUAL(1, redraws.active());
	LONGS_EQUAL(123, currentUIMode);
	CHECK_FALSE(session::navigation.active().shared_model_refresh.consume(0));
}
TEST(LaunchStyleMenu, seven_segment_peer_refresh_draws_live_value_without_editing_it) {
	display_instance.oled = false;
	local_menu.selectEncoderAction(1);
	session::Scope owner(session::Id::Remote);
	remote_menu.refresh_shared_model();
	STRCMP_EQUAL("Fill", text.active().c_str());
	CHECK(clip.launchStyle == LaunchStyle::FILL);
}
TEST(LaunchStyleMenu, refresh_of_other_clip_keeps_its_selection_and_viewport) {
	Clip other;
	other.launchStyle = LaunchStyle::ONCE;
	remote_menu.clip = &other;
	{
		session::Scope owner(session::Id::Remote);
		remote_menu.setupAndCheckAvailability();
		remote_menu.scrollPos = 1;
	}
	local_menu.selectEncoderAction(1);
	session::Scope owner(session::Id::Remote);
	remote_menu.refresh_shared_model();
	LONGS_EQUAL(2, remote_menu.currentOption);
	LONGS_EQUAL(1, remote_menu.scrollPos);
	LONGS_EQUAL(0, redraws.active());
	CHECK(other.launchStyle == LaunchStyle::ONCE);
}
TEST(LaunchStyleMenu, oled_boundary_attempt_does_not_publish_an_unchanged_value) {
	clip.launchStyle = LaunchStyle::ONCE;
	local_menu.setupAndCheckAvailability();
	local_menu.selectEncoderAction(1);
	CHECK(clip.launchStyle == LaunchStyle::ONCE);
	CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
}
TEST(LaunchStyleMenu, absent_target_does_not_enter_edit_or_redraw) {
	local_menu.clip = nullptr;
	currentUIMode = 123;
	CHECK_FALSE(local_menu.setupAndCheckAvailability());
	local_menu.selectEncoderAction(1);
	local_menu.refresh_shared_model();
	LONGS_EQUAL(123, currentUIMode);
	LONGS_EQUAL(0, redraws.active());
	CHECK(text.active().empty());
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
