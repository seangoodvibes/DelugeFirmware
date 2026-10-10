#pragma once
#include "gui/ui/ui_navigation_state.h"
#include <cstdint>
#include <span>
#include <string>
#define PLACE_SDRAM_BSS
namespace session = deluge::gui::ui_session;
enum class LaunchStyle { DEFAULT, FILL, ONCE };
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
struct display_fixture {
	bool oled = true;
	bool haveOLED() { return oled; }
	bool have7SEG() { return !oled; }
	void setText(const char* value, bool, int, bool) { text.active() = value; }
};
inline display_fixture display_instance;
inline auto* display = &display_instance;
namespace indicator_leds {
inline void ledBlinkTimeout(int, bool) {
}
} // namespace indicator_leds
namespace deluge::l10n {
enum class String { STRING_FOR_DEFAULT_LAUNCH, STRING_FOR_FILL_LAUNCH, STRING_FOR_ONCE_LAUNCH };
inline const char* get(String value) {
	static const char* names[] = {"Default", "Fill", "Once"};
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
	void drawCurrentOption();
	int32_t currentOption = 0, scrollPos = 0;
};
} // namespace deluge::gui
