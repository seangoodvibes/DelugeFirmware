#pragma once
#include "gui/ui/ui_session.h"
#include <cstdint>
#include <string>
#include <vector>
namespace session = deluge::gui::ui_session;
enum class OutputType { AUDIO, SYNTH, MIDI_OUT, CV };
enum class AudioInputChannel { SPECIFIC_OUTPUT };
constexpr int OLED_MAIN_TOPMOST_PIXEL = 0, kTextSpacingX = 1, kTextSpacingY = 1;
constexpr int kTextTitleSizeY = 1, kTextTitleSpacingX = 1, OLED_MAIN_WIDTH_PIXELS = 128;
struct name_fixture {
	const char* value = "track";
	const char* get() { return value; }
};
struct Output {
	OutputType type = OutputType::SYNTH;
	Output* next = nullptr;
	name_fixture name;
};
struct Instrument : Output {};
struct NonAudioInstrument : Instrument {
	int getChannel() { return 1; }
};
struct AudioOutput : Output {
	AudioOutput() { type = OutputType::AUDIO; }
	AudioInputChannel inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
	Output* source = nullptr;
	int writes = 0;
	Output* getOutputRecordingFrom() { return source; }
	bool canRecordFrom(Output* value) const {
		return value && value != this && value->type != OutputType::MIDI_OUT && value->type != OutputType::CV;
	}
	void setOutputRecordingFrom(Output* value) {
		source = value;
		++writes;
	}
};
struct Song {
	bool has_clip = true;
	void* getCurrentClip() { return has_clip ? this : nullptr; }
	Output* firstOutput = nullptr;
	int getNumOutputs() {
		int count = 0;
		for (auto* output = firstOutput; output; output = output->next)
			++count;
		return count;
	}
	Output* getOutputFromIndex(int index) {
		auto* output = firstOutput;
		while (output && index-- > 0)
			output = output->next;
		return index < 0 ? output : nullptr;
	}
};
inline Song* currentSong;
inline session::State<Output*> edited_outputs;
inline Output* getCurrentOutput() {
	return edited_outputs.active();
}
inline const char* getOutputTypeName(OutputType, int) {
	return "type";
}
struct ModControllableAudio {};
inline session::State<std::string> drawn_text;
namespace deluge::hid::display::oled_canvas {
struct Canvas {
	void drawStringCentred(const char* text, int, int, int) { drawn_text.active() = text; }
	void drawString(const char* text, int, int, int, int) { drawn_text.active() = text; }
	int getStringWidthInPixels(const char*, int) { return 1; }
};
} // namespace deluge::hid::display::oled_canvas
namespace deluge::hid::display {
struct OLED {
	static oled_canvas::Canvas& main_for_session() {
		static session::State<oled_canvas::Canvas> canvases;
		return canvases.active();
	}
	static void setupSideScroller(int, const char*, int, int, int, int, int, int, bool) {}
};
} // namespace deluge::hid::display
struct display_fixture {
	bool oled = false;
	bool haveOLED() { return oled; }
	void setScrollingText(const char* text, int) { drawn_text.active() = text; }
};
inline display_fixture physical_display;
inline auto* display = &physical_display;
inline void renderUIsForOled() {
}
namespace deluge::gui::menu_item {
struct MenuItem {
	virtual ~MenuItem() = default;
	virtual void beginSession(MenuItem*) {}
	virtual void selectEncoderAction(int32_t) {}
	virtual void drawPixelsForOled() {}
	virtual void refresh_shared_value() {}
	virtual bool isRelevant(ModControllableAudio*, int32_t) const { return true; }
	virtual bool shouldEnterSubmenu() { return true; }
};
} // namespace deluge::gui::menu_item
