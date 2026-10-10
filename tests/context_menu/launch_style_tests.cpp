#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/context_menu/clip_settings/launch_style.h"

#include "clip_membership.inc"

namespace deluge::gui {
#include "context_menu_input.inc"
}
using deluge::gui::context_menu::clip_settings::LaunchStyleMenu;
TEST_GROUP(LaunchStyleMenu) {
	Song song;
	Clip clip;
	LaunchStyleMenu local_menu, remote_menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		on_text = {};
		session::navigation = {};
		currentSong = &song;
		song.sessionClips.entries = {&clip};
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
		currentSong = nullptr;
		session::detail::active = session::Id::Local;
		on_text = {};
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
	song.arrangementOnlyClips.entries = {&other};
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
TEST(LaunchStyleMenu, departed_clip_is_not_read_or_edited_and_can_be_reattached) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& menu = owner == session::Id::Local ? local_menu : remote_menu;
		song.sessionClips.entries.clear();
		song.arrangementOnlyClips.entries.clear();
		clip.launchStyle = LaunchStyle::FILL;
		currentUIMode = 123;
		CHECK_FALSE(menu.setupAndCheckAvailability());
		LONGS_EQUAL(123, currentUIMode);
		menu.currentOption = 0;
		menu.refresh_shared_model();
		LONGS_EQUAL(0, menu.currentOption);
		currentUIMode = 0;
		menu.selectEncoderAction(1);
		CHECK(clip.launchStyle == LaunchStyle::FILL);
		song.arrangementOnlyClips.entries = {&clip};
		CHECK(menu.setupAndCheckAvailability());
		menu.selectEncoderAction(-1);
		CHECK(clip.launchStyle == LaunchStyle::DEFAULT);
	}
}
TEST(LaunchStyleMenu, missing_or_replaced_song_rejects_retained_clip) {
	currentSong = nullptr;
	CHECK_FALSE(local_menu.setupAndCheckAvailability());
	local_menu.selectEncoderAction(1);
	CHECK(clip.launchStyle == LaunchStyle::DEFAULT);
	Song replacement;
	currentSong = &replacement;
	CHECK_FALSE(local_menu.setupAndCheckAvailability());
	local_menu.refresh_shared_model();
	local_menu.selectEncoderAction(1);
	CHECK(clip.launchStyle == LaunchStyle::DEFAULT);
}
TEST(LaunchStyleMenu, display_callback_removing_clip_prevents_later_edit) {
	display_instance.oled = false;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& menu = owner == session::Id::Local ? local_menu : remote_menu;
		for (bool stale_selection : {false, true}) {
			song.sessionClips.entries = {&clip};
			clip.launchStyle = stale_selection ? LaunchStyle::FILL : LaunchStyle::DEFAULT;
			const auto original_style = clip.launchStyle;
			menu.currentOption = 0;
			on_text = [&] { song.sessionClips.entries.clear(); };
			menu.selectEncoderAction(1);
			CHECK(clip.launchStyle == original_style);
		}
	}
}
TEST(LaunchStyleMenu, display_callback_retargeting_menu_does_not_edit_replacement_clip) {
	display_instance.oled = false;
	Clip replacement;
	replacement.launchStyle = LaunchStyle::ONCE;
	song.arrangementOnlyClips.entries = {&replacement};
	on_text = [&] { local_menu.clip = &replacement; };
	local_menu.selectEncoderAction(1);
	CHECK(clip.launchStyle == LaunchStyle::DEFAULT);
	CHECK(replacement.launchStyle == LaunchStyle::ONCE);
	CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
}
TEST(LaunchStyleMenu, display_callback_changing_song_or_owner_cancels_edit) {
	display_instance.oled = false;
	Song replacement;
	replacement.sessionClips.entries = {&clip};
	for (bool replace_song : {false, true}) {
		currentSong = &song;
		local_menu.currentOption = 0;
		on_text = [&] {
			if (replace_song)
				currentSong = &replacement;
			else
				session::detail::active = session::Id::Remote;
		};
		local_menu.selectEncoderAction(1);
		session::detail::active = session::Id::Local;
		CHECK(clip.launchStyle == LaunchStyle::DEFAULT);
	}
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
