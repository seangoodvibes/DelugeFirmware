#include "CppUTest/TestHarness.h"
#include "gui/context_menu/clip_settings/clip_settings.h"
#include "gui/context_menu/clip_settings/launch_style.h"
#include "gui/ui/rename/rename_clip_ui.h"
using deluge::gui::context_menu::clip_settings::ClipSettingsMenu;
TEST_GROUP(ClipSettingsEntry) {
	Song song;
	Clip clip;
	ClipSettingsMenu menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		currentSong = &song;
		song.sessionClips.entries = {&clip};
		menu.clip = &clip;
		opened_menus = {};
		session_views = {};
		rename_clips = {};
		modes = {};
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
		currentSong = nullptr;
	}
};
TEST(ClipSettingsEntry, departed_clip_cannot_dispatch_any_option) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		song.sessionClips.entries.clear();
		for (int option = 0; option < 3; ++option) {
			menu.currentOption = option;
			CHECK_FALSE(menu.acceptCurrentOption());
			POINTERS_EQUAL(nullptr, opened_menus.active());
			POINTERS_EQUAL(nullptr, session_views.active().converted_clip);
			POINTERS_EQUAL(nullptr, rename_clips.active().clip);
		}
	}
}
TEST(ClipSettingsEntry, departed_clip_rejects_setup_options_and_encoder) {
	song.sessionClips.entries.clear();
	menu.currentOption = 2;
	CHECK_FALSE(menu.setupAndCheckAvailability());
	CHECK(menu.getOptions().empty());
	menu.selectEncoderAction(-1);
	LONGS_EQUAL(2, menu.currentOption);
}
TEST(ClipSettingsEntry, live_options_route_to_initiating_panel) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		CHECK(menu.setupAndCheckAvailability());
		menu.currentOption = 0;
		CHECK_FALSE(menu.acceptCurrentOption());
		POINTERS_EQUAL(&clip, session_views.active().converted_clip);
		menu.currentOption = 1;
		CHECK(menu.acceptCurrentOption());
		auto& launch = deluge::gui::context_menu::clip_settings::launch_style_for_session();
		POINTERS_EQUAL(&launch, opened_menus.active());
		POINTERS_EQUAL(&clip, launch.clip);
		menu.currentOption = 2;
		CHECK(menu.acceptCurrentOption());
		POINTERS_EQUAL(&rename_clips.active(), opened_menus.active());
		POINTERS_EQUAL(&clip, rename_clips.active().clip);
	}
}

TEST(ClipSettingsEntry, missing_song_and_null_clip_are_unavailable) {
	currentSong = nullptr;
	CHECK_FALSE(menu.setupAndCheckAvailability());
	CHECK(menu.getOptions().empty());
	CHECK_FALSE(menu.acceptCurrentOption());
	currentSong = &song;
	menu.clip = nullptr;
	CHECK_FALSE(menu.setupAndCheckAvailability());
	CHECK(menu.getOptions().empty());
	CHECK_FALSE(menu.acceptCurrentOption());
}
TEST(ClipSettingsEntry, arrangement_only_audio_clip_has_launch_and_rename_options) {
	song.sessionClips.entries.clear();
	song.arrangementOnlyClips.entries = {&clip};
	clip.type = ClipType::AUDIO;
	CHECK(menu.setupAndCheckAvailability());
	LONGS_EQUAL(2, menu.getOptions().size());
	CHECK(menu.acceptCurrentOption());
	POINTERS_EQUAL(&deluge::gui::context_menu::clip_settings::launch_style_for_session(), opened_menus.active());
	menu.currentOption = 1;
	CHECK(menu.acceptCurrentOption());
	POINTERS_EQUAL(&rename_clips.active(), opened_menus.active());
}
