#include "CppUTest/TestHarness.h"
#include "gui/context_menu/audio_input_selector.h"
#include "gui/menu_item/audio_clip/audio_source_selector.h"
using deluge::gui::context_menu::AudioInputSelector;
TEST_GROUP(AudioInputMenu) {
	Song song;
	AudioOutput output, other_output;
	Output first, second;
	AudioInputSelector local_menu, remote_menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		on_text = {};
		sdRoutineLock = false;
		root_available = true;
		session::navigation = {};
		modes = {};
		redraws = {};
		text = {};
		session_views = {};
		currentSong = &song;
		song.firstOutput = &first;
		first.next = &second;
		second.next = &output;
		output.next = &other_output;
		first.name.value = "first";
		second.name.value = "second";
		display_instance.oled = true;
		local_menu.audioOutput = remote_menu.audioOutput = &output;
		local_menu.setupAndCheckAvailability();
		session::Scope owner(session::Id::Remote);
		remote_menu.setupAndCheckAvailability();
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
		on_text = {};
		sdRoutineLock = false;
		root_available = true;
	}
};
TEST(AudioInputMenu, edits_start_from_shared_channel_before_peer_refresh) {
	for (bool oled : {true, false}) {
		display_instance.oled = oled;
		output.inputChannel = AudioInputChannel::NONE;
		local_menu.setupAndCheckAvailability();
		{
			session::Scope owner(session::Id::Remote);
			remote_menu.setupAndCheckAvailability();
			remote_menu.selectEncoderAction(1);
		}
		CHECK(output.inputChannel == AudioInputChannel::LEFT);
		local_menu.selectEncoderAction(1);
		CHECK(output.inputChannel == AudioInputChannel::RIGHT);
		CHECK(defaultAudioOutputInputChannel == AudioInputChannel::RIGHT);
	}
}
TEST(AudioInputMenu, encoder_commit_notifies_peer_and_refreshes_its_display) {
	local_menu.selectEncoderAction(1);
	session::Scope owner(session::Id::Remote);
	CHECK(session::navigation.active().shared_model_refresh.consume(0));
	currentUIMode = 123;
	remote_menu.refresh_shared_model();
	LONGS_EQUAL(1, remote_menu.currentOption);
	LONGS_EQUAL(123, currentUIMode);
	LONGS_EQUAL(1, redraws.active());
	display_instance.oled = false;
	remote_menu.refresh_shared_model();
	STRCMP_EQUAL("Left", text.active().c_str());
}
TEST(AudioInputMenu, pad_source_changes_refresh_peer_even_when_channel_stays_track) {
	output.inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
	output.source = &first;
	local_menu.setupAndCheckAvailability();
	{
		session::Scope owner(session::Id::Remote);
		remote_menu.setupAndCheckAvailability();
	}
	session_view_for_session().target = &second;
	local_menu.padAction(1, 1, 1);
	POINTERS_EQUAL(&second, output.source);
	session::Scope owner(session::Id::Remote);
	CHECK(session::navigation.active().shared_model_refresh.consume(0));
	remote_menu.refresh_shared_model();
	LONGS_EQUAL(1, redraws.active());
	deluge::hid::display::oled_canvas::Canvas canvas;
	remote_menu.renderOLED(canvas);
	STRCMP_EQUAL("second", text.active().c_str());
}
TEST(AudioInputMenu, refresh_preserves_other_output_selection_and_viewport) {
	other_output.inputChannel = AudioInputChannel::RIGHT;
	remote_menu.audioOutput = &other_output;
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
	CHECK(other_output.inputChannel == AudioInputChannel::RIGHT);
}

TEST(AudioInputMenu, leaving_peer_selected_track_clears_source_and_notifies_peer) {
	local_menu.setupAndCheckAvailability();
	output.inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
	output.source = &first;
	local_menu.selectEncoderAction(-1);
	CHECK(output.inputChannel == AudioInputChannel::OUTPUT);
	POINTERS_EQUAL(nullptr, output.source);
	CHECK(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
}
TEST(AudioInputMenu, track_entry_preserves_valid_source_and_repairs_removed_source) {
	Output removed;
	output.inputChannel = AudioInputChannel::OUTPUT;
	output.source = &second;
	local_menu.setupAndCheckAvailability();
	local_menu.selectEncoderAction(1);
	POINTERS_EQUAL(&second, output.source);
	output.inputChannel = AudioInputChannel::OUTPUT;
	output.source = &removed;
	local_menu.selectEncoderAction(1);
	POINTERS_EQUAL(&first, output.source);
	currentSong = nullptr;
	text.active() = "unchanged";
	deluge::hid::display::oled_canvas::Canvas canvas;
	local_menu.renderOLED(canvas);
	STRCMP_EQUAL("unchanged", text.active().c_str());
}
TEST(AudioInputMenu, mode_lock_and_missing_target_do_not_edit_or_redraw) {
	currentUIMode = 123;
	local_menu.selectEncoderAction(1);
	CHECK(output.inputChannel == AudioInputChannel::NONE);
	LONGS_EQUAL(0, redraws.active());
	currentUIMode = 0;
	local_menu.audioOutput = nullptr;
	CHECK_FALSE(local_menu.setupAndCheckAvailability());
	local_menu.selectEncoderAction(1);
	local_menu.refresh_shared_model();
	deluge::hid::display::oled_canvas::Canvas canvas;
	local_menu.renderOLED(canvas);
	LONGS_EQUAL(0, redraws.active());
	CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
}
TEST(AudioInputMenu, rejected_pad_sources_and_unchanged_boundaries_do_not_notify_peer) {
	session_view_for_session().target = &output;
	local_menu.padAction(0, 0, 1);
	CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
	first.type = OutputType::MIDI_OUT;
	session_view_for_session().target = &first;
	local_menu.padAction(0, 0, 1);
	CHECK(output.inputChannel == AudioInputChannel::NONE);
	local_menu.selectEncoderAction(-1);
	CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
}

TEST(AudioInputMenu, storage_locked_pad_selection_defers_without_mutation_and_retries_live_target) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& menu = owner == session::Id::Local ? local_menu : remote_menu;
		output.inputChannel = AudioInputChannel::NONE;
		output.source = nullptr;
		output.assignments = 0;
		session::navigation = {};
		redraws = {};
		text = {};
		menu.setupAndCheckAvailability();
		session_view_for_session().target = &first;
		sdRoutineLock = true;
		CHECK(menu.padAction(0, 0, 1) == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE);
		CHECK(output.inputChannel == AudioInputChannel::NONE);
		POINTERS_EQUAL(nullptr, output.source);
		LONGS_EQUAL(0, output.assignments);
		LONGS_EQUAL(0, menu.currentOption);
		LONGS_EQUAL(0, redraws.active());
		CHECK(text.active().empty());
		const auto peer = owner == session::Id::Local ? session::Id::Remote : session::Id::Local;
		CHECK_FALSE(session::navigation.for_owner(peer).shared_model_refresh.consume(0));
		CHECK(menu.padAction(0, 0, 0) == ActionResult::DEALT_WITH);
		LONGS_EQUAL(0, output.assignments);
		session_view_for_session().target = &second;
		sdRoutineLock = false;
		CHECK(menu.padAction(0, 0, 1) == ActionResult::DEALT_WITH);
		CHECK(output.inputChannel == AudioInputChannel::SPECIFIC_OUTPUT);
		POINTERS_EQUAL(&second, output.source);
		LONGS_EQUAL(1, output.assignments);
		CHECK(session::navigation.for_owner(peer).shared_model_refresh.consume(0));
	}
}

TEST(AudioInputMenu, departed_target_is_rejected_before_reads_edits_or_greyout) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& menu = owner == session::Id::Local ? local_menu : remote_menu;
		output.inputChannel = AudioInputChannel::NONE;
		output.assignments = 0;
		second.next = nullptr;
		CHECK_FALSE(menu.setupAndCheckAvailability());
		menu.selectEncoderAction(1);
		session_view_for_session().target = &first;
		menu.padAction(0, 0, 1);
		CHECK(output.inputChannel == AudioInputChannel::NONE);
		LONGS_EQUAL(0, output.assignments);
		menu.refresh_shared_model();
		text.active() = "unchanged";
		deluge::hid::display::oled_canvas::Canvas canvas;
		menu.renderOLED(canvas);
		STRCMP_EQUAL("unchanged", text.active().c_str());
		uint32_t cols = 123, rows = 456;
		CHECK_FALSE(menu.getGreyoutColsAndRows(&cols, &rows));
		LONGS_EQUAL(456, rows);
		second.next = &output;
		CHECK(menu.setupAndCheckAvailability());
		menu.selectEncoderAction(1);
		CHECK(output.inputChannel == AudioInputChannel::LEFT);
	}
}
TEST(AudioInputMenu, missing_song_and_wrong_output_type_are_not_available) {
	currentSong = nullptr;
	CHECK_FALSE(local_menu.setupAndCheckAvailability());
	local_menu.selectEncoderAction(1);
	CHECK(output.inputChannel == AudioInputChannel::NONE);
	currentSong = &song;
	output.type = OutputType::SYNTH;
	CHECK_FALSE(local_menu.setupAndCheckAvailability());
	local_menu.selectEncoderAction(1);
	CHECK(output.inputChannel == AudioInputChannel::NONE);
}
TEST(AudioInputMenu, greyout_without_root_does_not_write_masks) {
	root_available = false;
	uint32_t cols = 123, rows = 456;
	CHECK_FALSE(local_menu.getGreyoutColsAndRows(&cols, &rows));
	LONGS_EQUAL(123, cols);
	LONGS_EQUAL(456, rows);
}
TEST(AudioInputMenu, feedback_removing_output_cancels_channel_and_source_edit) {
	display_instance.oled = false;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& menu = owner == session::Id::Local ? local_menu : remote_menu;
		second.next = &output;
		output.inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		output.source = &first;
		defaultAudioOutputInputChannel = AudioInputChannel::RIGHT;
		on_text = [&] { second.next = &other_output; };
		menu.selectEncoderAction(-1);
		CHECK(output.inputChannel == AudioInputChannel::SPECIFIC_OUTPUT);
		POINTERS_EQUAL(&first, output.source);
		LONGS_EQUAL(0, output.assignments);
		CHECK(defaultAudioOutputInputChannel == AudioInputChannel::RIGHT);
		CHECK_FALSE(session::navigation.for_owner(session::Id::Local).shared_model_refresh.consume(0));
		CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
	}
}
TEST(AudioInputMenu, feedback_retargeting_menu_does_not_edit_either_output) {
	display_instance.oled = false;
	other_output.inputChannel = AudioInputChannel::BALANCED;
	on_text = [&] { local_menu.audioOutput = &other_output; };
	local_menu.selectEncoderAction(1);
	CHECK(output.inputChannel == AudioInputChannel::NONE);
	CHECK(other_output.inputChannel == AudioInputChannel::BALANCED);
	CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
}
TEST(AudioInputMenu, feedback_changing_song_or_owner_cancels_edit) {
	display_instance.oled = false;
	Song replacement;
	replacement.firstOutput = &output;
	for (bool replace_song : {false, true}) {
		currentSong = &song;
		on_text = [&] {
			if (replace_song)
				currentSong = &replacement;
			else
				session::detail::active = session::Id::Remote;
		};
		local_menu.selectEncoderAction(1);
		session::detail::active = session::Id::Local;
		CHECK(output.inputChannel == AudioInputChannel::NONE);
	}
}
TEST(AudioInputMenu, feedback_model_change_is_not_overwritten_by_pending_edit) {
	display_instance.oled = false;
	for (bool change_channel : {false, true}) {
		output.inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		output.source = &first;
		defaultAudioOutputInputChannel = AudioInputChannel::RIGHT;
		on_text = [&] {
			if (change_channel)
				output.inputChannel = AudioInputChannel::LEFT;
			else
				output.source = &second;
		};
		local_menu.selectEncoderAction(-1);
		CHECK(output.inputChannel == (change_channel ? AudioInputChannel::LEFT : AudioInputChannel::SPECIFIC_OUTPUT));
		POINTERS_EQUAL(change_channel ? &first : &second, output.source);
		LONGS_EQUAL(0, output.assignments);
		CHECK(defaultAudioOutputInputChannel == AudioInputChannel::RIGHT);
		CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
	}
}

TEST_GROUP(AudioSourceEntry) {
	Song song;
	Clip clip;
	AudioOutput local_output, remote_output;
	deluge::gui::menu_item::audio_clip::AudioSourceSelector entry;
	void setup() override {
		session::detail::active = session::Id::Local;
		currentSong = &song;
		song.sessionClips.entries = {&clip};
		song.selected_clips.for_owner(session::Id::Local) = &clip;
		song.selected_clips.for_owner(session::Id::Remote) = &clip;
		song.firstOutput = &local_output;
		local_output.next = &remote_output;
		selected_outputs.for_owner(session::Id::Local) = &local_output;
		selected_outputs.for_owner(session::Id::Remote) = &remote_output;
		opened_menus = {};
		output_lookups = 0;
	}
	void teardown() override {
		for (auto owner : {session::Id::Local, session::Id::Remote}) {
			session::Scope scope(owner);
			deluge::gui::context_menu::audio_input_selector_for_session().audioOutput = nullptr;
		}
		session::detail::active = session::Id::Local;
		currentSong = nullptr;
	}
};
TEST(AudioSourceEntry, valid_entry_opens_only_the_initiating_panels_selector) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		POINTERS_EQUAL(deluge::gui::menu_item::NO_NAVIGATION, entry.selectButtonPress());
		auto& selector = deluge::gui::context_menu::audio_input_selector_for_session();
		POINTERS_EQUAL(&selector, opened_menus.active());
		POINTERS_EQUAL(selected_outputs.active(), selector.audioOutput);
	}
	CHECK(opened_menus.for_owner(session::Id::Local) != opened_menus.for_owner(session::Id::Remote));
}
TEST(AudioSourceEntry, unavailable_output_does_not_open_context_menu) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		song.firstOutput = nullptr;
		entry.selectButtonPress();
		POINTERS_EQUAL(nullptr, opened_menus.active());
	}
}
TEST(AudioSourceEntry, departed_clip_is_rejected_before_output_lookup) {
	song.sessionClips.entries.clear();
	entry.selectButtonPress();
	LONGS_EQUAL(0, output_lookups);
	POINTERS_EQUAL(nullptr, opened_menus.active());
}

TEST(AudioSourceEntry, missing_song_clip_and_wrong_output_type_do_not_open) {
	currentSong = nullptr;
	entry.selectButtonPress();
	LONGS_EQUAL(0, output_lookups);
	currentSong = &song;
	song.selected_clips.active() = nullptr;
	entry.selectButtonPress();
	LONGS_EQUAL(0, output_lookups);
	song.selected_clips.active() = &clip;
	local_output.type = OutputType::SYNTH;
	entry.selectButtonPress();
	POINTERS_EQUAL(nullptr, opened_menus.active());
}
