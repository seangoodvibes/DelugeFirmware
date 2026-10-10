#pragma once
#include "gui/menu_item/shared_value_cache.h"
#include "gui/ui_timer_state.h"
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
namespace deluge {
template <typename T>
using vector = std::vector<T>;
namespace l10n {
enum class String { STRING_FOR_OFF, name };
inline const char* get(String value) {
	return value == String::STRING_FOR_OFF ? "Off" : "Developer SysEx";
}
inline std::string_view getView(String value) {
	return get(value);
}
} // namespace l10n
} // namespace deluge
enum class RuntimeFeatureSettingType { DevSysexAllowed, ShowBatteryLevel };
struct setting_fixture {
	int32_t value = 0;
	deluge::l10n::String displayName = deluge::l10n::String::name;
};
struct runtime_settings_fixture {
	bool isOn(RuntimeFeatureSettingType) const { return true; }
	std::array<setting_fixture, 1> settings;
};
inline runtime_settings_fixture runtimeFeatureSettings;
inline std::vector<int32_t> noise_values;
inline size_t noise_index = 0;
inline int32_t getNoise() {
	return noise_values.at(noise_index++);
}
inline void intToHex(int32_t value, char* buffer) {
	std::snprintf(buffer, 9, "%08X", static_cast<uint32_t>(value));
}
namespace deluge::gui::menu_item {
enum class OptType { FULL };
class Selection {
	SharedValueCache<int32_t> values;

public:
	virtual ~Selection() = default;
	virtual void readCurrentValue() = 0;
	virtual void writeCurrentValue() = 0;
	virtual deluge::vector<std::string_view> getOptions(OptType) = 0;
	virtual std::string_view getName() const = 0;
	virtual std::string_view getTitle() const = 0;
	void setValue(int32_t value) { values.set(value); }
	int32_t getValue() {
		return values.get([this] { readCurrentValue(); });
	}
	void commit(int32_t value) {
		setValue(value);
		writeCurrentValue();
		values.committed();
	}
};
} // namespace deluge::gui::menu_item

namespace session = deluge::gui::ui_session;
inline session::State<std::string> battery_text;
inline session::State<int> battery_redraws;
enum class ActionResult { DEALT_WITH };
struct ModControllableAudio {};
namespace deluge::gui::menu_item {
class MenuItem {
public:
	virtual ~MenuItem() = default;
	virtual bool isRelevant(ModControllableAudio*, int32_t) const { return true; }
	virtual void drawPixelsForOled() {}
	virtual void beginSession(MenuItem*) {}
	virtual ActionResult timerCallback() { return ActionResult::DEALT_WITH; }
};
} // namespace deluge::gui::menu_item
struct battery_display_fixture {
	bool oled = false;
	bool haveOLED() { return oled; }
	void setScrollingText(const char* text) {
		if (!oled)
			battery_text.active() = text;
	}
};
inline battery_display_fixture battery_display;
inline auto* display = &battery_display;
inline void renderUIsForOled() {
	++battery_redraws.active();
}
struct battery_timer_fixture {
	UITimerState state;
	void setTimer(TimerName name, int32_t delay) { state.set(name, 0, delay); }
};
inline battery_timer_fixture uiTimerManager;
namespace deluge::hid::display::oled_canvas {
struct Canvas {
	void drawStringCentredShrinkIfNecessary(const char* text, int, int, int) { battery_text.active() = text; }
};
} // namespace deluge::hid::display::oled_canvas
namespace deluge::hid::display {
struct OLED {
	static oled_canvas::Canvas& main_for_session() {
		static session::State<oled_canvas::Canvas> canvases;
		return canvases.active();
	}
};
} // namespace deluge::hid::display
