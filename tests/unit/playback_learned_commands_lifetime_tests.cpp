#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace playback_learned_commands_lifetime_test {
constexpr int UI_MODE_MIDI_LEARN = 1, IS_A_PC = 32, kMaxNumSections = 2, kMIDIKeyInputLatency = 7;
int currentUIMode;
struct MIDICable {};
struct Owner {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch{lifetime}; }
};
struct Command {
	bool matched = false;
	bool equalsNoteOrCC(MIDICable*, int, int) { return matched; }
};
struct Clip : Owner {
	Command muteMIDICommand{true};
};
struct Song : Owner {
	struct {
		Command launchMIDICommand;
	} sections[kMaxNumSections];
	struct {
		std::vector<Clip*> clips;
		int getNumElements() { return clips.size(); }
		Clip* getClipAtIndex(int index) { return clips.at(index); }
	} sessionClips;
};
Song* currentSong;
std::function<void()> on_global, on_switch, on_section, on_refresh, on_pc;
std::function<void(Clip*, int*)> on_toggle;
int global_calls, switch_calls, section_calls, toggle_calls, refreshes, pc_calls, learn_calls;
bool global_used, pc_used;
struct {
	bool active = false;
	bool hasPlaybackActive() { return active; }
} arrangement;
struct {
	void armSection(int, int latency) {
		LONGS_EQUAL(kMIDIKeyInputLatency, latency);
		++section_calls;
		if (on_section)
			on_section();
	}
	void toggleClipStatus(Clip* clip, int* index, bool, int latency) {
		LONGS_EQUAL(kMIDIKeyInputLatency, latency);
		++toggle_calls;
		if (on_toggle)
			on_toggle(clip, index);
	}
} session;
namespace Buttons {
bool isShiftButtonPressed() {
	return false;
}
} // namespace Buttons
struct {
	void requestRendering(void*, int, unsigned) {
		++refreshes;
		if (on_refresh)
			on_refresh();
	}
} session_view;
auto& session_view_for_session() {
	return session_view;
}
void* getRootUI() {
	return nullptr;
}
struct UI {
	bool pcReceivedForMidiLearn(MIDICable&, int, int) {
		++pc_calls;
		if (on_pc)
			on_pc();
		return pc_used;
	}
} ui;
UI* getCurrentUI() {
	return &ui;
}
struct {
	void pcReceivedForMIDILearn(MIDICable&, int, int) { ++learn_calls; }
} view;
auto& view_for_session() {
	return view;
}
struct PlaybackHandler {
	bool tryGlobalMIDICommands(MIDICable&, int, int) {
		++global_calls;
		if (on_global)
			on_global();
		return global_used;
	}
	bool tryGlobalMIDICommandsOff(MIDICable& cable, int channel, int note) {
		return tryGlobalMIDICommands(cable, channel, note);
	}
	void switchToSession() {
		arrangement.active = false;
		++switch_calls;
		if (on_switch)
			on_switch();
	}
	bool offerNoteToLearnedThings(MIDICable&, bool, int32_t, int32_t);
	void programChangeReceived(MIDICable&, int32_t, int32_t);
};
#include "playback_learned_commands_lifetime.inc"
} // namespace playback_learned_commands_lifetime_test
using namespace playback_learned_commands_lifetime_test;
TEST_GROUP(playback_learned_commands_lifetime) {
	std::unique_ptr<Song> song;
	std::unique_ptr<Clip> first, second;
	MIDICable cable;
	PlaybackHandler handler;
	void reset() {
		on_global = on_switch = on_section = on_refresh = on_pc = {};
		on_toggle = {};
		global_calls = switch_calls = section_calls = toggle_calls = refreshes = pc_calls = learn_calls = 0;
		global_used = pc_used = false;
		arrangement.active = false;
		currentUIMode = 0;
		song = std::make_unique<Song>();
		first = std::make_unique<Clip>();
		second = std::make_unique<Clip>();
		song->sessionClips.clips = {first.get(), second.get()};
		currentSong = song.get();
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void setup() override {
		reset();
	}
	void teardown() override {
		on_global = on_switch = on_section = on_refresh = on_pc = {};
		on_toggle = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	bool send(bool on = true) {
		return handler.offerNoteToLearnedThings(cable, on, 2, 60);
	}
};
TEST(playback_learned_commands_lifetime, live_command_visits_sections_and_all_matching_clips) {
	song->sections[0].launchMIDICommand.matched = true;
	song->sections[1].launchMIDICommand.matched = true;
	CHECK(send());
	LONGS_EQUAL(2, section_calls);
	LONGS_EQUAL(2, toggle_calls);
	LONGS_EQUAL(2, refreshes);
}
TEST(playback_learned_commands_lifetime, note_off_reports_matches_without_launching_or_toggling) {
	song->sections[0].launchMIDICommand.matched = true;
	CHECK(send(false));
	LONGS_EQUAL(0, section_calls);
	LONGS_EQUAL(0, toggle_calls);
}
TEST(playback_learned_commands_lifetime, global_callback_song_deletion_stops_traversal) {
	on_global = [&] { song.reset(); };
	CHECK_FALSE(send());
	LONGS_EQUAL(0, toggle_calls);
}
TEST(playback_learned_commands_lifetime, section_callback_deletion_stops_remaining_routes) {
	song->sections[0].launchMIDICommand.matched = true;
	on_section = [&] { song.reset(); };
	CHECK(send());
	LONGS_EQUAL(1, section_calls);
	LONGS_EQUAL(0, toggle_calls);
}
TEST(playback_learned_commands_lifetime, switch_callback_clip_deletion_prevents_toggle) {
	arrangement.active = true;
	on_switch = [&] { second.reset(); };
	CHECK(send());
	LONGS_EQUAL(1, switch_calls);
	LONGS_EQUAL(0, toggle_calls);
}
TEST(playback_learned_commands_lifetime, switch_callback_row_replacement_prevents_toggle) {
	arrangement.active = true;
	on_switch = [&] { song->sessionClips.clips[1] = first.get(); };
	CHECK(send());
	LONGS_EQUAL(0, toggle_calls);
}
TEST(playback_learned_commands_lifetime, toggle_callback_song_deletion_prevents_refresh) {
	on_toggle = [&](Clip*, int*) { song.reset(); };
	CHECK(send());
	LONGS_EQUAL(1, toggle_calls);
	LONGS_EQUAL(0, refreshes);
}
TEST(playback_learned_commands_lifetime, deliberate_current_clip_deletion_allows_remaining_clip) {
	on_toggle = [&](Clip* clip, int* index) {
		if (clip == second.get()) {
			song->sessionClips.clips.erase(song->sessionClips.clips.begin() + *index);
			second.reset();
		}
	};
	CHECK(send());
	LONGS_EQUAL(2, toggle_calls);
	LONGS_EQUAL(2, refreshes);
}
TEST(playback_learned_commands_lifetime, cleared_clip_list_stops_without_stale_index_access) {
	on_toggle = [&](Clip*, int*) {
		song->sessionClips.clips.clear();
		first.reset();
		second.reset();
	};
	CHECK(send());
	LONGS_EQUAL(1, toggle_calls);
	LONGS_EQUAL(1, refreshes);
}
TEST(playback_learned_commands_lifetime, negative_adjusted_index_ends_after_refresh) {
	on_toggle = [&](Clip*, int* index) { *index = -1; };
	CHECK(send());
	LONGS_EQUAL(1, toggle_calls);
	LONGS_EQUAL(1, refreshes);
}
TEST(playback_learned_commands_lifetime, refresh_callback_song_reuse_cancels_iteration) {
	on_refresh = [&] {
		auto* raw = song.get();
		raw->~Song();
		new (raw) Song;
	};
	CHECK(send());
	LONGS_EQUAL(1, toggle_calls);
}
TEST(playback_learned_commands_lifetime, session_switch_cancels_and_restores_owner) {
	on_toggle = [&](Clip*, int*) { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	CHECK(send());
	LONGS_EQUAL(0, refreshes);
	CHECK(deluge::gui::ui_session::current() == deluge::gui::ui_session::Id::Local);
}
TEST(playback_learned_commands_lifetime, program_learn_fallback_preserves_consumed_behavior) {
	currentUIMode = UI_MODE_MIDI_LEARN;
	handler.programChangeReceived(cable, 2, 60);
	LONGS_EQUAL(1, learn_calls);
	pc_used = true;
	handler.programChangeReceived(cable, 2, 60);
	LONGS_EQUAL(1, learn_calls);
	LONGS_EQUAL(0, global_calls);
}
TEST(playback_learned_commands_lifetime, program_learn_song_deletion_cancels_fallback) {
	currentUIMode = UI_MODE_MIDI_LEARN;
	on_pc = [&] { song.reset(); };
	handler.programChangeReceived(cable, 2, 60);
	LONGS_EQUAL(1, pc_calls);
	LONGS_EQUAL(0, learn_calls);
}
TEST(playback_learned_commands_lifetime, malformed_program_input_does_not_dispatch) {
	handler.programChangeReceived(cable, -1, 60);
	handler.programChangeReceived(cable, 16, 60);
	handler.programChangeReceived(cable, 2, 128);
	LONGS_EQUAL(0, global_calls);
	LONGS_EQUAL(0, toggle_calls);
}
