#include "CppUTest/TestHarness.h"
#include "hid/led/pad_leds_state.h"
#include "hid/mirror_protocol.h"
#include <vector>

namespace pad_column_routing_test {
static std::vector<uint8_t> bytes;
}
namespace deluge::hid::mirror {
bool panel_byte(uint8_t byte) {
	pad_column_routing_test::bytes.push_back(byte);
	return false;
}
} // namespace deluge::hid::mirror
namespace pad_column_routing_test {
namespace session = deluge::gui::ui_session;
struct Panel {
	PadLEDs::PadFrame frame;
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
};
static session::State<Panel> panels;
static Panel& state() {
	return panels.active();
}
static auto& image_for_session() {
	return state().image;
}
static bool local_output() {
	return session::current() == session::Id::Local;
}
static RGB prepareColour(int, int, RGB colour) {
	return RGB(colour.r + 1, colour.g + 2, colour.b + 3);
}
struct AudioEngine {
	static void logAction(const char*) {}
};
static int physical_writes;
namespace PadLEDs {
void flashMainPad(int32_t x, int32_t y, int32_t colour);
}
struct PIC {
	static void flashMainPad(size_t) { ++physical_writes; }
	static void flashMainPadWithColourIdx(size_t, int32_t) { ++physical_writes; }
	static void setColourForTwoColumns(size_t, const std::array<RGB, kDisplayHeight * 2>&) { ++physical_writes; }
};
#include "pad_column_render.inc"
TEST_GROUP(PadColumnRouting){void setup() override{session::detail::active = session::Id::Local;
bytes.clear();
physical_writes = 0;
panels.for_owner(session::Id::Local) = {};
panels.for_owner(session::Id::Remote) = {};
} // namespace pad_column_routing_test
}
;
TEST(PadColumnRouting, remote_columns_serialize_prepared_rgb_in_column_order_including_sidebar) {
	session::Scope remote(session::Id::Remote);
	for (int x = 0; x < kDisplayWidth + kSideBarWidth; x += 2) {
		bytes.clear();
		for (int y = 0; y < kDisplayHeight; ++y) {
			image_for_session()[y][x] = RGB(x, y, 10);
			image_for_session()[y][x + 1] = RGB(x + 1, y, 20);
		}
		sortLedsForCol(x + 1);
		LONGS_EQUAL(1 + 6 * kDisplayHeight, bytes.size());
		LONGS_EQUAL(1 + x / 2, bytes[0]);
		LONGS_EQUAL(bytes.size(), deluge::hid::mirror::protocol::panel_command_size(bytes[0]));
		for (int column = 0; column < 2; ++column) {
			for (int y = 0; y < kDisplayHeight; ++y) {
				const int offset = 1 + 3 * (column * kDisplayHeight + y);
				LONGS_EQUAL(x + column + 1, bytes[offset]);
				LONGS_EQUAL(y + 2, bytes[offset + 1]);
				LONGS_EQUAL((column ? 20 : 10) + 3, bytes[offset + 2]);
				LONGS_EQUAL(bytes[offset], state().frame.colours[y][x + column].r);
			}
		}
	}
	LONGS_EQUAL(0, physical_writes);
	LONGS_EQUAL(0, panels.for_owner(session::Id::Local).frame.revision);
}
TEST(PadColumnRouting, local_column_output_remains_physical) {
	sortLedsForCol(0);
	LONGS_EQUAL(1, physical_writes);
	CHECK_TRUE(bytes.empty());
	LONGS_EQUAL(0, panels.for_owner(session::Id::Remote).frame.revision);
}
}

namespace pad_column_routing_test {
TEST(PadColumnRouting, remote_flash_routes_every_pad_with_optional_colour_selection) {
	session::Scope remote(session::Id::Remote);
	for (int colour : {0, 1, 2}) {
		for (int x = 0; x < kDisplayWidth; ++x) {
			for (int y = 0; y < kDisplayHeight; ++y) {
				bytes.clear();
				PadLEDs::flashMainPad(x, y, colour);
				LONGS_EQUAL(colour > 0 ? 2 : 1, bytes.size());
				if (colour > 0)
					LONGS_EQUAL(10 + colour, bytes.front());
				LONGS_EQUAL(24 + x * kDisplayHeight + y, bytes.back());
				for (uint8_t command : bytes)
					LONGS_EQUAL(1, deluge::hid::mirror::protocol::panel_command_size(command));
				CHECK_TRUE(state().frame.flashes[y][x].revision > 0);
			}
		}
	}
	LONGS_EQUAL(0, physical_writes);
	LONGS_EQUAL(0, panels.for_owner(session::Id::Local).frame.flashes[0][0].revision);
}
TEST(PadColumnRouting, invalid_flash_coordinates_emit_nothing_for_either_owner) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		PadLEDs::flashMainPad(-1, 0, 1);
		PadLEDs::flashMainPad(kDisplayWidth, 0, 1);
		PadLEDs::flashMainPad(0, -1, 1);
		PadLEDs::flashMainPad(0, kDisplayHeight, 1);
	}
	CHECK_TRUE(bytes.empty());
	LONGS_EQUAL(0, physical_writes);
}
TEST(PadColumnRouting, local_flash_preserves_physical_output) {
	PadLEDs::flashMainPad(0, 0, 0);
	PadLEDs::flashMainPad(15, 7, 2);
	LONGS_EQUAL(2, physical_writes);
	CHECK_TRUE(bytes.empty());
	LONGS_EQUAL(0, panels.for_owner(session::Id::Remote).frame.flashes[0][0].revision);
}
} // namespace pad_column_routing_test
