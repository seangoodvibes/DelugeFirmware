#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "hid/display/seven_segment_frame.h"
#include <vector>

namespace seven_segment_routing_test {
namespace gui = deluge::gui;
using deluge::hid::display::SevenSegmentFrame;
static std::vector<uint8_t> bytes;
static int physical_writes, emulated_writes, sysex_writes;
namespace mirror {
bool panel_byte(uint8_t byte) {
	bytes.push_back(byte);
	return false;
}
} // namespace mirror
struct NumericLayer {
	uint8_t fixedDot = 255;
	void render(uint8_t* output) {
		for (size_t i = 0; i < kNumericDisplayLength; ++i)
			output[i] = i + 1;
	}
};
struct Panel {
	bool popupActive = false;
	NumericLayer popup;
	NumericLayer* topLayer = nullptr;
	SevenSegmentFrame frame;
};
static gui::ui_session::State<Panel> panels;
static Panel& panel_state() {
	return panels.active();
}
static bool have_oled_screen;
struct OLED {
	static void renderEmulated7Seg(const std::array<uint8_t, kNumericDisplayLength>&) { ++emulated_writes; }
};
struct PIC {
	static void update7SEG(const std::array<uint8_t, kNumericDisplayLength>&) { ++physical_writes; }
};
struct HIDSysex {
	static void sendDisplayIfChanged() { ++sysex_writes; }
};
struct SevenSegment {
	void render();
};
#include "seven_segment_render.inc"
TEST_GROUP(SevenSegmentRouting){void setup() override{gui::ui_session::detail::active = gui::ui_session::Id::Local;
panels.for_owner(gui::ui_session::Id::Local) = {};
panels.for_owner(gui::ui_session::Id::Remote) = {};
bytes.clear();
physical_writes = emulated_writes = sysex_writes = 0;
have_oled_screen = false;
} // namespace seven_segment_routing_test
}
;
TEST(SevenSegmentRouting, remote_popup_sends_completed_digits_and_fixed_dot_without_hardware) {
	gui::ui_session::Scope remote(gui::ui_session::Id::Remote);
	panel_state().popupActive = true;
	panel_state().popup.fixedDot = 2;
	SevenSegment{}.render();
	LONGS_EQUAL(5, bytes.size());
	LONGS_EQUAL(224, bytes[0]);
	for (size_t i = 0; i < 4; ++i)
		LONGS_EQUAL((i + 1) | (i == 2 ? 128 : 0), bytes[i + 1]);
	LONGS_EQUAL(0, physical_writes);
	LONGS_EQUAL(0, sysex_writes);
}
TEST(SevenSegmentRouting, remote_empty_layer_sends_blank_display) {
	gui::ui_session::Scope remote(gui::ui_session::Id::Remote);
	SevenSegment{}.render();
	LONGS_EQUAL(5, bytes.size());
	for (size_t i = 1; i < bytes.size(); ++i)
		LONGS_EQUAL(0, bytes[i]);
}
TEST(SevenSegmentRouting, remote_oled_emulation_does_not_send_numeric_or_physical_output) {
	gui::ui_session::Scope remote(gui::ui_session::Id::Remote);
	have_oled_screen = true;
	SevenSegment{}.render();
	CHECK_TRUE(bytes.empty());
	LONGS_EQUAL(1, emulated_writes);
	LONGS_EQUAL(0, physical_writes);
	LONGS_EQUAL(0, sysex_writes);
}
TEST(SevenSegmentRouting, local_numeric_render_preserves_physical_and_sysex_output) {
	SevenSegment{}.render();
	CHECK_TRUE(bytes.empty());
	LONGS_EQUAL(1, physical_writes);
	LONGS_EQUAL(1, sysex_writes);
}
}
