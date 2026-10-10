#pragma once
#include "gui/ui/ui_navigation_state.h"
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>
#define PLACE_SDRAM_BSS
namespace session = deluge::gui::ui_session;
enum class LaunchStyle { DEFAULT, FILL, ONCE };
enum class AudioInputChannel { NONE, LEFT, RIGHT, STEREO, BALANCED, MIX, OUTPUT, SPECIFIC_OUTPUT };
enum class OutputType { AUDIO, SYNTH, MIDI_OUT, CV };
struct Output {
	OutputType type = OutputType::SYNTH;
	Output* next = nullptr;
	struct name_fixture {
		const char* value = "track";
		const char* get() const { return value; }
	} name;
};
struct AudioOutput : Output {
	AudioOutput() { type = OutputType::AUDIO; }
	AudioInputChannel inputChannel = AudioInputChannel::NONE;
	Output* source = nullptr;
	int assignments = 0;
	bool canRecordFrom(const Output* output) const {
		return output && output != this && output->type != OutputType::MIDI_OUT && output->type != OutputType::CV;
	}
	Output* getOutputRecordingFrom() { return source; }
	void setOutputRecordingFrom(Output* output) {
		source = output;
		++assignments;
	}
	void clearRecordingFrom() { setOutputRecordingFrom(nullptr); }
};
struct Clip;
struct clip_list_fixture {
	std::vector<Clip*> entries;
	int32_t getNumElements() const { return entries.size(); }
	Clip* getClipAtIndex(int32_t index) const { return entries[index]; }
};
struct Song {
	Output* firstOutput = nullptr;
	clip_list_fixture sessionClips, arrangementOnlyClips;
	bool contains_clip_for_undo(const Clip* clip);
};
inline Song* currentSong = nullptr;
inline AudioInputChannel defaultAudioOutputInputChannel = AudioInputChannel::NONE;
enum class ActionResult { DEALT_WITH, REMIND_ME_OUTSIDE_CARD_ROUTINE };
inline bool sdRoutineLock = false;
namespace deluge::hid::display::oled_canvas {
struct Canvas {
	void drawString(const char*, int, int, int, int, int, int);
};
} // namespace deluge::hid::display::oled_canvas
constexpr int OLED_MAIN_HEIGHT_PIXELS = 64, OLED_MAIN_WIDTH_PIXELS = 128, kTextSpacingX = 6, kTextSpacingY = 8;
struct Clip {
	LaunchStyle launchStyle = LaunchStyle::DEFAULT;
};
constexpr int UI_MODE_NONE = 0;
inline session::State<int> modes;
#define currentUIMode modes.active()
inline session::State<int> redraws;
inline session::State<std::string> text;
inline void renderUIsForOled() {
	++redraws.active();
}
inline std::function<void()> on_text;
struct display_fixture {
	bool oled = true;
	bool haveOLED() { return oled; }
	bool have7SEG() { return !oled; }
	void popupTextTemporary(const char* value) { text.active() = value; }
	void setText(const char* value, bool, int, bool) {
		text.active() = value;
		if (on_text)
			on_text();
	}
};
inline display_fixture display_instance;
inline auto* display = &display_instance;
namespace indicator_leds {
inline void ledBlinkTimeout(int, bool) {
}
} // namespace indicator_leds
namespace deluge::l10n {
enum class String {
	STRING_FOR_DEFAULT_LAUNCH,
	STRING_FOR_FILL_LAUNCH,
	STRING_FOR_ONCE_LAUNCH,
	STRING_FOR_AUDIO_SOURCE,
	STRING_FOR_DISABLED,
	STRING_FOR_LEFT_INPUT,
	STRING_FOR_RIGHT_INPUT,
	STRING_FOR_STEREO_INPUT,
	STRING_FOR_BALANCED_INPUT,
	STRING_FOR_MIX_PRE_FX,
	STRING_FOR_MIX_POST_FX,
	STRING_FOR_TRACK
};
inline const char* get(String value) {
	static const char* names[] = {"Default", "Fill",   "Once",     "Audio source", "Off",    "Left",
	                              "Right",   "Stereo", "Balanced", "Master",       "Output", "Track"};
	return names[static_cast<int>(value)];
}
} // namespace deluge::l10n
namespace deluge::gui {
class ContextMenu {
public:
	virtual ~ContextMenu() = default;
	virtual void selectEncoderAction(int8_t);
	virtual bool setupAndCheckAvailability() { return true; }
	virtual bool canSeeViewUnderneath() { return true; }
	virtual const char* getTitle() = 0;
	virtual std::span<const char*> getOptions() = 0;
	virtual bool isCurrentOptionAvailable() { return true; }
	virtual void refresh_shared_model() {}
	virtual bool getGreyoutColsAndRows(uint32_t*, uint32_t*) { return false; }
	virtual ActionResult padAction(int32_t, int32_t, int32_t) { return ActionResult::DEALT_WITH; }
	virtual void renderOLED(deluge::hid::display::oled_canvas::Canvas&) {}
	void drawCurrentOption();
	int32_t currentOption = 0, scrollPos = 0;
};
} // namespace deluge::gui

inline void deluge::hid::display::oled_canvas::Canvas::drawString(const char* value, int, int, int, int, int, int) {
	text.active() = value;
}
struct session_view_fixture {
	Output* target = nullptr;
	Output* getOutputFromPad(int32_t, int32_t) { return target; }
	uint32_t getGreyedOutRowsNotRepresentingOutput(AudioOutput*) { return 0; }
};
inline session::State<session_view_fixture> session_views;
inline session_view_fixture& session_view_for_session() {
	return session_views.active();
}
inline bool root_available = true;
inline session_view_fixture* getRootUI() {
	return root_available ? &session_view_for_session() : nullptr;
}
inline session_view_fixture* getUIUpOneLevel() {
	return &session_view_for_session();
}
