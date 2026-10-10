#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/menu_item/audio_clip/specific_output_source_selector.h"
using deluge::gui::menu_item::audio_clip::SpecificSourceOutputSelector;
TEST_GROUP(SourceMenu) {
	Song song;
	AudioOutput local_output, remote_output;
	Output first, second, third;
	SpecificSourceOutputSelector menu;
	void setup() override {
		session::detail::active = session::Id::Local;
		currentSong = &song;
		song.firstOutput = &first;
		first.next = &second;
		second.next = &third;
		third.next = &local_output;
		local_output.next = &remote_output;
		first.name.value = "first";
		second.name.value = "second";
		third.name.value = "third";
		local_output.source = &first;
		remote_output.source = &second;
		edited_outputs.for_owner(session::Id::Local) = &local_output;
		edited_outputs.for_owner(session::Id::Remote) = &remote_output;
		physical_display.oled = false;
		drawn_text = {};
		session::navigation = {};
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(SourceMenu, opening_other_panel_cannot_redirect_edits_or_rendering) {
	menu.beginSession(nullptr);
	{
		session::Scope owner(session::Id::Remote);
		menu.beginSession(nullptr);
	}
	menu.selectEncoderAction(1);
	POINTERS_EQUAL(&second, local_output.source);
	POINTERS_EQUAL(&second, remote_output.source);
	LONGS_EQUAL(1, local_output.writes);
	LONGS_EQUAL(0, remote_output.writes);
	{
		session::Scope owner(session::Id::Remote);
		menu.selectEncoderAction(1);
	}
	POINTERS_EQUAL(&third, remote_output.source);
	menu.drawFor7seg();
	STRCMP_EQUAL("second", drawn_text.active().c_str());
	menu.drawPixelsForOled();
	STRCMP_EQUAL("second", drawn_text.active().c_str());
}
TEST(SourceMenu, same_track_reads_shared_value_before_render_and_next_edit) {
	edited_outputs.for_owner(session::Id::Remote) = &local_output;
	menu.beginSession(nullptr);
	{
		session::Scope owner(session::Id::Remote);
		menu.beginSession(nullptr);
		menu.selectEncoderAction(1);
	}
	menu.drawFor7seg();
	STRCMP_EQUAL("second", drawn_text.active().c_str());
	menu.selectEncoderAction(1);
	POINTERS_EQUAL(&third, local_output.source);
}
TEST(SourceMenu, output_reordering_does_not_change_source_shown_or_next_selection) {
	menu.beginSession(nullptr);
	song.firstOutput = &second;
	second.next = &first;
	first.next = &third;
	menu.drawFor7seg();
	STRCMP_EQUAL("first", drawn_text.active().c_str());
	menu.selectEncoderAction(1);
	POINTERS_EQUAL(&third, local_output.source);
}
TEST(SourceMenu, appended_output_is_selectable_without_reopening) {
	first.next = &second;
	second.next = nullptr;
	local_output.source = &second;
	menu.beginSession(nullptr);
	second.next = &third;
	menu.selectEncoderAction(1);
	POINTERS_EQUAL(&third, local_output.source);
}

TEST(SourceMenu, external_source_change_is_visible_to_both_display_types) {
	menu.beginSession(nullptr);
	local_output.source = &third;
	menu.drawFor7seg();
	STRCMP_EQUAL("third", drawn_text.active().c_str());
	menu.drawPixelsForOled();
	STRCMP_EQUAL("third", drawn_text.active().c_str());
	menu.selectEncoderAction(-1);
	POINTERS_EQUAL(&second, local_output.source);
}
TEST(SourceMenu, invalid_and_empty_sources_are_repaired_only_when_opened) {
	Output removed;
	local_output.source = &removed;
	menu.drawFor7seg();
	STRCMP_EQUAL("No track", drawn_text.active().c_str());
	LONGS_EQUAL(0, local_output.writes);
	menu.beginSession(nullptr);
	POINTERS_EQUAL(&first, local_output.source);
	song.firstOutput = &local_output;
	local_output.next = nullptr;
	menu.beginSession(nullptr);
	POINTERS_EQUAL(nullptr, local_output.source);
	menu.drawPixelsForOled();
	STRCMP_EQUAL("No track", drawn_text.active().c_str());
	menu.selectEncoderAction(1);
	POINTERS_EQUAL(nullptr, local_output.source);
}
TEST(SourceMenu, skips_self_midi_and_cv_and_saturates_large_offsets) {
	second.type = OutputType::MIDI_OUT;
	third.type = OutputType::CV;
	menu.beginSession(nullptr);
	menu.selectEncoderAction(INT32_MAX);
	POINTERS_EQUAL(&remote_output, local_output.source);
	menu.selectEncoderAction(INT32_MIN);
	POINTERS_EQUAL(&first, local_output.source);
	menu.selectEncoderAction(0);
	POINTERS_EQUAL(&first, local_output.source);
}
TEST(SourceMenu, missing_or_non_audio_context_is_not_dereferenced) {
	for (int scenario = 0; scenario < 4; ++scenario) {
		currentSong = scenario == 0 ? nullptr : &song;
		song.has_clip = scenario != 1;
		edited_outputs.active() = scenario == 2 ? nullptr : &first;
		menu.beginSession(nullptr);
		menu.selectEncoderAction(1);
		menu.drawFor7seg();
		STRCMP_EQUAL("No track", drawn_text.active().c_str());
		menu.drawPixelsForOled();
		STRCMP_EQUAL("No track", drawn_text.active().c_str());
		CHECK_FALSE(menu.isRelevant(nullptr, 0));
	}
	LONGS_EQUAL(0, local_output.writes);
	LONGS_EQUAL(0, remote_output.writes);
}

TEST(SourceMenu, source_changes_queue_peer_refresh_without_drawing_the_other_panel) {
	edited_outputs.for_owner(session::Id::Remote) = &local_output;
	menu.beginSession(nullptr);
	{
		session::Scope remote(session::Id::Remote);
		menu.beginSession(nullptr);
	}
	menu.selectEncoderAction(1);
	STRCMP_EQUAL("second", drawn_text.active().c_str());
	STRCMP_EQUAL("first", drawn_text.for_owner(session::Id::Remote).c_str());
	CHECK_FALSE(session::navigation.active().shared_model_refresh.consume(0));
	{
		session::Scope remote(session::Id::Remote);
		CHECK(session::navigation.active().shared_model_refresh.consume(0));
		menu.refresh_shared_value();
		STRCMP_EQUAL("second", drawn_text.active().c_str());
		CHECK_FALSE(session::navigation.active().shared_model_refresh.consume(0));
		menu.selectEncoderAction(-1);
	}
	CHECK(session::navigation.active().shared_model_refresh.consume(0));
	menu.refresh_shared_value();
	STRCMP_EQUAL("first", drawn_text.active().c_str());
	menu.selectEncoderAction(0);
	CHECK_FALSE(session::navigation.for_owner(session::Id::Remote).shared_model_refresh.consume(0));
	LONGS_EQUAL(2, local_output.writes);
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
