#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "util/lifetime.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace global_midi_command_lifetime_test {
enum class GlobalMIDICommand {
	PLAYBACK_RESTART,
	PLAY,
	RECORD,
	LOOP,
	LOOP_CONTINUOUS_LAYERING,
	REDO,
	UNDO,
	FILL,
	NEXT_SONG,
	SHIFT,
	TAP,
	TRANSPOSE
};
enum class RecordingMode { OFF, ARRANGEMENT };
constexpr int kNumGlobalMIDICommands = 12, kMIDIKeyInputLatency = 7, UI_MODE_NONE = 0;
int currentUIMode = UI_MODE_NONE;
struct MIDICable {};
std::function<void()> on_command;
std::vector<GlobalMIDICommand> executed;
void command(GlobalMIDICommand kind) {
	executed.push_back(kind);
	if (on_command)
		on_command();
}
struct Song {
	deluge::lifetime::lifetime_source lifetime;
	auto watch_lifetime() { return deluge::lifetime::lifetime_watch{lifetime}; }
	void changeFillMode(bool) { command(GlobalMIDICommand::FILL); }
	void loadNextSong() { command(GlobalMIDICommand::NEXT_SONG); }
};
Song* currentSong;
struct Mapping {
	bool note_match = false, channel_match = false;
	bool equalsChannelOrZone(MIDICable*, int) { return channel_match; }
	bool equalsNoteOrCC(MIDICable*, int, int) { return note_match; }
};
struct {
	std::array<Mapping, kNumGlobalMIDICommands> globalMIDICommands;
} midiEngine;
namespace MIDITranspose {
void doTranspose(bool, int) {
	command(GlobalMIDICommand::TRANSPOSE);
}
} // namespace MIDITranspose
namespace Buttons {
void commandToggleShift(bool) {
	command(GlobalMIDICommand::SHIFT);
}
bool isShiftButtonPressed() {
	return true;
}
} // namespace Buttons
namespace indicator_leds {
enum class LED { SHIFT };
void setLedState(LED, bool) {
}
} // namespace indicator_leds
void* getCurrentUI() {
	return nullptr;
}
void* getRootUI() {
	return nullptr;
}
struct {
	bool allowed = true;
	bool allowedToDoReversion() { return allowed; }
} actionLogger;
struct PlaybackHandler {
	RecordingMode recording = RecordingMode::OFF;
	int pendingGlobalMIDICommandNumClustersWritten = 42;
	void forceResetPlayPos(Song*, bool) { command(GlobalMIDICommand::PLAYBACK_RESTART); }
	void playButtonPressed(int) { command(GlobalMIDICommand::PLAY); }
	void recordButtonPressed() { command(GlobalMIDICommand::RECORD); }
	void tryLoopCommand(GlobalMIDICommand kind) { command(kind); }
	void pend_global_m_id_i_command(GlobalMIDICommand kind) { command(kind); }
	void tapTempoButtonPress(bool) { command(GlobalMIDICommand::TAP); }
	bool tryGlobalMIDICommands(MIDICable&, int32_t, int32_t);
};
#include "global_midi_command_lifetime.inc"
} // namespace global_midi_command_lifetime_test
using namespace global_midi_command_lifetime_test;
TEST_GROUP(global_midi_command_lifetime) {
	std::unique_ptr<Song> song;
	MIDICable cable;
	PlaybackHandler handler;
	void setup() override {
		song = std::make_unique<Song>();
		currentSong = song.get();
		executed.clear();
		on_command = {};
		midiEngine = {};
		currentUIMode = UI_MODE_NONE;
		actionLogger.allowed = true;
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void teardown() override {
		on_command = {};
		deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Local;
	}
	void match(GlobalMIDICommand kind) {
		midiEngine.globalMIDICommands[static_cast<int>(kind)].note_match = true;
	}
	bool send() {
		return handler.tryGlobalMIDICommands(cable, 2, 60);
	}
};
TEST(global_midi_command_lifetime, live_shared_mapping_preserves_command_order) {
	match(GlobalMIDICommand::PLAY);
	match(GlobalMIDICommand::RECORD);
	CHECK(send());
	LONGS_EQUAL(2, executed.size());
	CHECK(executed[0] == GlobalMIDICommand::PLAY);
	CHECK(executed[1] == GlobalMIDICommand::RECORD);
}
TEST(global_midi_command_lifetime, first_command_song_deletion_cancels_remaining_commands) {
	match(GlobalMIDICommand::PLAY);
	match(GlobalMIDICommand::RECORD);
	on_command = [&] { song.reset(); };
	CHECK(send());
	LONGS_EQUAL(1, executed.size());
}
TEST(global_midi_command_lifetime, same_address_song_replacement_cancels_remaining_commands) {
	match(GlobalMIDICommand::PLAY);
	match(GlobalMIDICommand::RECORD);
	on_command = [&] {
		auto* raw = song.get();
		raw->~Song();
		new (raw) Song;
	};
	CHECK(send());
	LONGS_EQUAL(1, executed.size());
}
TEST(global_midi_command_lifetime, session_switch_cancels_remaining_commands) {
	match(GlobalMIDICommand::PLAY);
	match(GlobalMIDICommand::RECORD);
	on_command = [] { deluge::gui::ui_session::detail::active = deluge::gui::ui_session::Id::Remote; };
	CHECK(send());
	LONGS_EQUAL(1, executed.size());
}
TEST(global_midi_command_lifetime, retired_song_rejects_command_entry) {
	match(GlobalMIDICommand::PLAY);
	song->lifetime.retire();
	CHECK_FALSE(send());
	LONGS_EQUAL(0, executed.size());
}
TEST(global_midi_command_lifetime, undo_mapping_keeps_deferred_scheduling_behavior) {
	match(GlobalMIDICommand::UNDO);
	CHECK(send());
	LONGS_EQUAL(1, executed.size());
	CHECK(executed[0] == GlobalMIDICommand::UNDO);
	LONGS_EQUAL(0, handler.pendingGlobalMIDICommandNumClustersWritten);
	executed.clear();
	actionLogger.allowed = false;
	CHECK(send());
	LONGS_EQUAL(0, executed.size());
}
