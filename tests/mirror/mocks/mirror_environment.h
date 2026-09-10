#pragma once
#include "definitions_cxx.hpp"
#include "gui/ui/ui_navigation_state.h"
#include "gui/ui/ui_session.h"
#include "gui/ui_timer_state.h"
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>
#undef PLACE_SDRAM_BSS
#define PLACE_SDRAM_BSS

namespace fixture {
inline double now = 1;
inline uint32_t uart_space = 65536;
inline std::vector<std::vector<uint8_t>> panel_frames, oled_frames;
struct Event {
	int key, value;
	deluge::gui::ui_session::Id owner;
};
inline std::vector<Event> events;
inline std::vector<deluge::gui::ui_session::Id> main_pad_owners, sidebar_pad_owners;
inline ActionResult input_result = ActionResult::DEALT_WITH;
inline bool defer_encoder = false;
inline std::vector<std::pair<int, bool>> leds;
inline std::vector<std::pair<int, std::array<uint8_t, 4>>> knobs;
inline int encoder_queues = 0, encoder_dispatches = 0, clears = 0, flushes = 0, focused = 0;
inline std::function<void()> on_input, on_exit_editor, on_stop_audition, on_send, on_discovery, on_encoder;
inline int note_stops = 0;
} // namespace fixture
class MIDICable {
public:
	int connectionFlags = 1;
	size_t space = 65536;
	std::vector<std::vector<uint8_t>> sent;
	size_t sendBufferSpace() { return space; }
	void sendSysex(uint8_t* data, size_t size) {
		sent.emplace_back(data, data + size);
		if (size > 7 && data[7] == 9 && fixture::on_discovery) {
			fixture::on_discovery();
			return;
		}
		auto callback = fixture::on_send;
		if (callback)
			callback();
	}
};
struct USBDevice {
	bool canHaveMIDISent = false;
	MIDICable* cable[1]{};
	int discarded_queues = 0;
	uint32_t connection_generation = 0;
	void discard_queued_non_sys_ex() { ++discarded_queues; }
};
inline USBDevice connectedUSBMIDIDevices[1][4];
inline bool developerSysexCodeReceived = false, sdRoutineLock = false;
inline double getSystemTime() {
	return fixture::now;
}
inline uint32_t uartGetTxBufferSpace(int) {
	return fixture::uart_space;
}
inline void setOutputState(int, int, bool) {
}
inline bool spiBusCurrentlySending = false;
inline struct {
	uintptr_t N0SA_n = 0;
} mock_dma;
#define DMACn(channel) mock_dma
inline uint8_t* oledFrameQueue[3]{};
inline void enqueueOLEDFrame(uint8_t* frame) {
	fixture::oled_frames.emplace_back(frame, frame + 768);
}
struct MockDisplay {
	bool oled = true;
	std::string popup;
	bool haveOLED() { return oled; }
	void popupTextTemporary(const char* text) { popup = text; }
};
inline MockDisplay physical_display;
inline MockDisplay* display = &physical_display;
struct UI {
	void focusRegained() { ++fixture::focused; }
};
inline UI ui;
inline UI* getCurrentUI() {
	return &ui;
}
inline std::function<void()> on_remote_render, on_ui_timers;
inline bool defer_ui_render = false;
inline void doAnyPendingUIRendering() {
	if (on_remote_render)
		on_remote_render();
	if (!defer_ui_render) {
		auto& navigation = deluge::gui::ui_session::navigation.active();
		navigation.main_rows_dirty = navigation.side_rows_dirty = 0;
		navigation.oled_dirty = false;
	}
}
inline void renderingNeededRegardlessOfUI() {
}
struct Editor {
	int exits = 0;
	void exitCompletely() {
		++exits;
		if (fixture::on_exit_editor)
			fixture::on_exit_editor();
	}
};
inline Editor editor;
inline Editor& sound_editor_for_session() {
	return editor;
}
struct UITimers {
	void routine() {
		if (on_ui_timers)
			on_ui_timers();
	}
	UITimerState state;
	void setTimer(TimerName timer, int32_t ms) { state.set(timer, 0, ms); }
	bool isTimerSet(TimerName timer) { return state.get(timer).active; }
	void unsetTimer(TimerName timer) { state.unset(timer, 0); }
	bool paused = false;
	void pause_for_mirror() { paused = true; }
	void resume_from_mirror() { paused = false; }
};
inline UITimers uiTimerManager;
struct Song {
	void stopAllAuditioning() {
		if (fixture::on_stop_audition)
			fixture::on_stop_audition();
	}
	void stopAllMIDIAndGateNotesPlaying() { ++fixture::note_stops; }
};
inline Song song;
inline Song* currentSong = &song;
struct Playback {
	int playbackState = 0;
	bool external = false;
	bool isExternalClockActive() { return external; }
};
inline Playback playbackHandler;
namespace AudioEngine {
inline void* firstRecorder = nullptr;
inline bool audioRoutineLocked = false;
inline void killAllVoices() {
}
} // namespace AudioEngine
inline struct {
	bool processStarted = false;
} stemExport;
inline struct {
	void flushUSBMIDIOutput() {}
	void flushMIDI() { ++fixture::flushes; }
} midiEngine;
namespace Buttons {
inline void ignoreCurrentShiftForSticky() {
}
inline void noPressesHappening(bool) {
}
inline ActionResult buttonAction(uint8_t key, bool on, bool) {
	fixture::events.push_back({key, on, deluge::gui::ui_session::current()});
	if (fixture::on_input)
		fixture::on_input();
	return fixture::input_result;
}
} // namespace Buttons
namespace deluge::hid {
namespace button {
constexpr uint8_t BACK = 170;
}
struct Pad {
	int x, y;
	explicit Pad(uint8_t key) : x(key % 18), y(key / 18) {}
	static bool isPad(uint8_t key) { return key < 144; }
};
namespace display {
struct Screensaver {
	static void noteActivity() {}
};
struct OLED {
	inline static uint8_t pixels[6][128]{};
	inline static std::array<uint8_t, 768> remote_pixels{};
	inline static bool remote_frame_available = false;
	static void invalidate_remote_frame() { remote_frame_available = false; }
	static std::optional<uint32_t> copy_remote_frame(std::span<uint8_t> destination) {
		if (!remote_frame_available || destination.size() != remote_pixels.size())
			return std::nullopt;
		std::copy(remote_pixels.begin(), remote_pixels.end(), destination.begin());
		return 1;
	}
	static uint8_t (*local_image())[128] { return pixels; }
	static void sendMainImage() {}
};
} // namespace display
namespace encoders {
struct Counter {
	int32_t value = 0;
	int32_t take() {
		auto v = value;
		value = 0;
		return v;
	}
};
inline Counter functions[4], mods[2];
inline Counter& functionEncoderAt(size_t i) {
	return functions[i];
}
inline Counter& modEncoderAt(size_t i) {
	return mods[i];
}
inline bool queued = false;
inline bool queue_session_encoder(uint8_t, int32_t) {
	if (queued)
		return false;
	queued = true;
	++fixture::encoder_queues;
	return true;
}
inline bool interpret_session_encoders(bool) {
	++fixture::encoder_dispatches;
	if (fixture::on_encoder)
		fixture::on_encoder();
	if (!fixture::defer_encoder)
		queued = false;
	return true;
}
inline bool session_encoders_pending() {
	return queued;
}
inline void clear_session_encoders() {
	queued = false;
	++fixture::clears;
}
} // namespace encoders
} // namespace deluge::hid
inline struct Matrix {
	ActionResult padAction(int x, int y, int velocity) {
		return Buttons::buttonAction(y * 18 + x, velocity != 0, false);
	}
	void noPressesHappening(bool) {}
} matrixDriver;
namespace PIC {
inline void setLEDOn(uint8_t led) {
	fixture::leds.emplace_back(led, true);
}
inline void setLEDOff(uint8_t led) {
	fixture::leds.emplace_back(led, false);
}
inline void setGoldKnobIndicator(uint8_t knob, std::array<uint8_t, 4> values) {
	fixture::knobs.emplace_back(knob, values);
}
inline void replay_mirror_bytes(std::span<const uint8_t> bytes) {
	fixture::panel_frames.emplace_back(bytes.begin(), bytes.end());
}
} // namespace PIC
namespace PadLEDs {
inline void sendOutMainPadColours() {
	fixture::main_pad_owners.push_back(deluge::gui::ui_session::current());
}
inline void sendOutSidebarColours() {
	fixture::sidebar_pad_owners.push_back(deluge::gui::ui_session::current());
}
} // namespace PadLEDs
