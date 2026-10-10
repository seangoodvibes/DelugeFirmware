#include "CppUTest/TestHarness.h"
#include "gui/context_menu/audio_input_selector.h"
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
