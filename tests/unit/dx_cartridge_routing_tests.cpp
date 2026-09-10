#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <algorithm>
#include <string_view>

namespace dx_cartridge_routing_test {
namespace ui_session = deluge::gui::ui_session;
constexpr int kOLEDMenuNumOptionsVisible = 6;
struct DX7Cartridge {
	int patch_count = 32;
	int numPatches() { return patch_count; }
};
bool openFile(std::string_view path, DX7Cartridge* cartridge) {
	cartridge->patch_count = path == "small" ? 2 : 32;
	return true;
}
struct test_display {
	bool oled = true;
	bool haveOLED() { return oled; }
} display_instance;
auto* display = &display_instance;
struct DxCartridge {
	struct session_state {
		DX7Cartridge* cartridge = nullptr;
		int32_t current_value = 0;
		int scroll_position = 0;
	};
	ui_session::State<session_state> session_states;
	session_state& state_for_session();
	bool tryLoad(std::string_view path);
	void selectEncoderAction(int32_t offset);
	int loaded_patch = -1, loaded_count = 0, redraws = 0;
	void loadPatch() {
		loaded_patch = state_for_session().current_value;
		loaded_count = state_for_session().cartridge->patch_count;
	}
	void readValueAgain() { ++redraws; }
	~DxCartridge() {
		delete session_states.for_owner(ui_session::Id::Local).cartridge;
		delete session_states.for_owner(ui_session::Id::Remote).cartridge;
	}
};
#include "dx_cartridge_routing.inc"
TEST_GROUP(DxCartridgeRouting){void setup() override{display_instance.oled = true;
} // namespace dx_cartridge_routing_test
}
;
TEST(DxCartridgeRouting, remote_load_and_selection_preserve_local_cartridge_and_scroll) {
	DxCartridge menu;
	{
		ui_session::Scope local(ui_session::Id::Local);
		CHECK(menu.tryLoad("large"));
		menu.selectEncoderAction(12);
		LONGS_EQUAL(12, menu.loaded_patch);
		LONGS_EQUAL(11, menu.state_for_session().scroll_position);
	}
	{
		ui_session::Scope remote(ui_session::Id::Remote);
		CHECK(menu.tryLoad("small"));
		menu.selectEncoderAction(1);
		LONGS_EQUAL(1, menu.loaded_patch);
		LONGS_EQUAL(2, menu.loaded_count);
		LONGS_EQUAL(0, menu.state_for_session().scroll_position);
	}
	{
		ui_session::Scope local(ui_session::Id::Local);
		LONGS_EQUAL(12, menu.state_for_session().current_value);
		LONGS_EQUAL(11, menu.state_for_session().scroll_position);
		menu.selectEncoderAction(1);
		LONGS_EQUAL(13, menu.loaded_patch);
		LONGS_EQUAL(32, menu.loaded_count);
		CHECK(menu.tryLoad("large"));
		LONGS_EQUAL(0, menu.state_for_session().current_value);
	}
	ui_session::Scope remote(ui_session::Id::Remote);
	LONGS_EQUAL(1, menu.state_for_session().current_value);
	LONGS_EQUAL(2, menu.state_for_session().cartridge->patch_count);
}
TEST(DxCartridgeRouting, uninitialized_or_empty_cartridge_does_not_load_a_patch) {
	DxCartridge menu;
	menu.selectEncoderAction(1);
	LONGS_EQUAL(0, menu.redraws);
	CHECK(menu.tryLoad("large"));
	menu.state_for_session().cartridge->patch_count = 0;
	menu.selectEncoderAction(1);
	LONGS_EQUAL(0, menu.redraws);
}
TEST(DxCartridgeRouting, numeric_display_preserves_scroll_and_clamped_selection_does_not_reload) {
	DxCartridge menu;
	display_instance.oled = false;
	CHECK(menu.tryLoad("small"));
	menu.state_for_session().scroll_position = 3;
	menu.selectEncoderAction(99);
	LONGS_EQUAL(1, menu.loaded_patch);
	LONGS_EQUAL(3, menu.state_for_session().scroll_position);
	LONGS_EQUAL(1, menu.redraws);
	menu.selectEncoderAction(1);
	LONGS_EQUAL(1, menu.redraws);
	menu.selectEncoderAction(-99);
	LONGS_EQUAL(0, menu.loaded_patch);
	LONGS_EQUAL(2, menu.redraws);
}
} // namespace dx_cartridge_routing_test
