#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>
namespace midi_device_menu_routing_test {
namespace ui_session = deluge::gui::ui_session;
namespace etl {
template <typename T, size_t N>
using vector = std::vector<T>;
}
constexpr int lowestDeviceNum = -3, kOLEDMenuNumOptionsVisible = 3;
struct MenuItem {};
struct MIDICable {
	int connectionFlags = 1;
	const char* name = "device";
	const char* getDisplayName() { return name; }
};
std::array<MIDICable, 9> cables;
namespace MIDIDeviceManager {
struct hosted_list {
	int getNumElements() { return 6; }
} hostedMIDIDevices;
} // namespace MIDIDeviceManager
struct editor {
	MIDICable* currentMIDICable = nullptr;
};
ui_session::State<editor> editors;
editor& sound_editor_for_session() {
	return editors.active();
}
struct test_display {
	bool haveOLED() { return true; }
} display_instance;
auto* display = &display_instance;
struct Devices {
	ui_session::State<int32_t> scroll_positions, values;
	int32_t& scroll_for_session();
	int getValue() { return values.active(); }
	void setValue(int value) { values.active() = value; }
	MIDICable* getCable(int index) { return &cables.at(index + 3); }
	void beginSession(MenuItem*);
	int32_t computeScrollForSelected(int32_t);
	void selectEncoderAction(int32_t);
	void drawPixelsForOled();
	std::vector<std::string_view> drawn_names;
	int selected_row = -1;
	void drawItemsForOled(const std::vector<std::string_view>& names, int selected) {
		drawn_names = names;
		selected_row = selected;
	}
	void drawValue() { drawPixelsForOled(); }
};
#include "midi_device_menu_routing.inc"
TEST_GROUP(MidiDeviceMenuRouting){void setup() override{editors = {};
cables = {};
cables[0].name = "DIN";
cables[1].connectionFlags = 0;
cables[2].connectionFlags = 0;
for (int i = 3; i < 9; ++i)
	cables[i].name = "USB";
} // namespace midi_device_menu_routing_test
}
;
TEST(MidiDeviceMenuRouting, opening_remote_menu_does_not_move_local_viewport) {
	Devices menu;
	{
		ui_session::Scope local(ui_session::Id::Local);
		menu.beginSession(nullptr);
		for (int i = 0; i < 5; ++i)
			menu.selectEncoderAction(1);
		LONGS_EQUAL(4, menu.getValue());
		LONGS_EQUAL(2, menu.scroll_for_session());
	}
	{
		ui_session::Scope remote(ui_session::Id::Remote);
		menu.beginSession(nullptr);
		menu.drawPixelsForOled();
		LONGS_EQUAL(-3, menu.getValue());
		LONGS_EQUAL(-3, menu.scroll_for_session());
		STRCMP_EQUAL("DIN", menu.drawn_names.front().data());
		LONGS_EQUAL(0, menu.selected_row);
		menu.selectEncoderAction(1);
		LONGS_EQUAL(0, menu.getValue());
		LONGS_EQUAL(1, menu.selected_row);
	}
	ui_session::Scope local(ui_session::Id::Local);
	menu.drawPixelsForOled();
	LONGS_EQUAL(2, menu.scroll_for_session());
	LONGS_EQUAL(2, menu.selected_row);
	CHECK(sound_editor_for_session().currentMIDICable == &cables[7]);
}
TEST(MidiDeviceMenuRouting, returning_from_device_settings_restores_only_owners_viewport) {
	Devices menu;
	MenuItem child;
	{
		ui_session::Scope local(ui_session::Id::Local);
		menu.beginSession(nullptr);
	}
	{
		ui_session::Scope remote(ui_session::Id::Remote);
		sound_editor_for_session().currentMIDICable = &cables[8];
		menu.beginSession(&child);
		menu.drawPixelsForOled();
		LONGS_EQUAL(5, menu.getValue());
		LONGS_EQUAL(3, menu.scroll_for_session());
		LONGS_EQUAL(2, menu.selected_row);
		menu.selectEncoderAction(1);
		LONGS_EQUAL(5, menu.getValue());
		LONGS_EQUAL(3, menu.scroll_for_session());
	}
	ui_session::Scope local(ui_session::Id::Local);
	LONGS_EQUAL(-3, menu.scroll_for_session());
	LONGS_EQUAL(-3, menu.getValue());
	CHECK(sound_editor_for_session().currentMIDICable == &cables[0]);
}
} // namespace midi_device_menu_routing_test
