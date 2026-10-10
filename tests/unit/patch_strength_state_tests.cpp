#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <vector>
namespace patch_strength_state_test {
namespace session = deluge::gui::ui_session;
enum class ParamManagerType { SOUND, GLOBAL };
enum class Polarity { BIPOLAR, UNIPOLAR };
constexpr int kMaxMenuPatchCableValue = 5000;
constexpr int kNoSelection = 255, kShortPressTime = 10;
struct MenuItem {};
struct PatchCable {
	Polarity polarity = Polarity::BIPOLAR;
	int32_t value = 0;
	int32_t get_current_value() { return value; }
	static bool hasPolarity(int) { return true; }
	static Polarity getDefaultPolarity(int) { return Polarity::UNIPOLAR; }
};
struct cable_set {
	int index = kNoSelection;
	PatchCable cable;
	PatchCable* patch_cables_[1] = {&cable};
	int getPatchCableIndex(int, int) { return index; }
};
using PatchCableSet = cable_set;
struct manager {
	ParamManagerType type = ParamManagerType::SOUND;
	int accesses = 0;
	bool matches_type(ParamManagerType expected) { return type == expected; }
	cable_set cables;
	cable_set* getPatchCableSet() {
		++accesses;
		return &cables;
	}
};
struct editor {
	manager* currentParamManager = nullptr;
	int8_t numberEditPos = 0;
};
session::State<editor> editors;
editor& sound_editor_for_session() {
	return editors.active();
}
struct RootUI {
	int scroll = 0;
	void horizontalEncoderAction(int offset) { scroll += offset; }
};
session::State<RootUI> automation_views, keyboard_views;
RootUI& automation_view_for_session() {
	return automation_views.active();
}
RootUI& keyboard_screen_for_session() {
	return keyboard_views.active();
}
RootUI* getRootUI() {
	return &automation_views.active();
}
namespace hid::button {
constexpr int X_ENC = 1;
}
namespace Buttons {
bool isButtonPressed(int) {
	return false;
}
} // namespace Buttons
namespace AudioEngine {
uint32_t audioSampleTimer = 0;
}
struct Decimal {
	void beginSession(MenuItem*) {}
	void horizontalEncoderAction(int) {}
};
struct PatchCableStrength : Decimal {
	session::State<uint32_t> scroll_delays;
	session::State<Polarity> polarities;
	session::State<bool> existing_cables;
	uint32_t& scroll_delay_for_session();
	Polarity& polarity_for_session();
	bool& cable_exists_for_session();
	void beginSession(MenuItem*);
	void horizontalEncoderAction(int32_t);
	void appendAdditionalDots(std::vector<uint8_t>&);
	bool isInHorizontalMenu() { return false; }
	int getS() { return 0; }
	int getDestinationDescriptor() { return 0; }
	void setPatchCablePolarity(Polarity polarity);
	void readCurrentValue();
	session::State<int> values;
	void setValue(int value) { values.active() = value; }

	void updatePolarityUI() {}
};
#include "patch_strength_state.inc"
TEST_GROUP(PatchStrengthState) {
	manager local_manager, remote_manager;
	void setup() override {
		editors = {};
		editors.for_owner(session::Id::Local).currentParamManager = &local_manager;
		editors.for_owner(session::Id::Remote).currentParamManager = &remote_manager;
		automation_views = {};
		keyboard_views = {};
		AudioEngine::audioSampleTimer = 100;
	}
};
TEST(PatchStrengthState, remote_existing_cable_does_not_replace_local_pending_polarity) {
	PatchCableStrength menu;
	{
		session::Scope local(session::Id::Local);
		menu.beginSession(nullptr);
		CHECK(!menu.cable_exists_for_session());
		CHECK(menu.polarity_for_session() == Polarity::UNIPOLAR);
	}
	{
		session::Scope remote(session::Id::Remote);
		remote_manager.cables.index = 0;
		menu.beginSession(nullptr);
		CHECK(menu.cable_exists_for_session());
		CHECK(menu.polarity_for_session() == Polarity::BIPOLAR);
		std::vector<uint8_t> dots;
		menu.appendAdditionalDots(dots);
		CHECK(dots.empty());
	}
	session::Scope local(session::Id::Local);
	CHECK(!menu.cable_exists_for_session());
	CHECK(menu.polarity_for_session() == Polarity::UNIPOLAR);
	std::vector<uint8_t> dots;
	menu.appendAdditionalDots(dots);
	LONGS_EQUAL(1, dots.size());
	LONGS_EQUAL(3, dots[0]);
}
TEST(PatchStrengthState, encoder_delay_and_menu_entry_are_independent) {
	PatchCableStrength menu;
	{
		session::Scope local(session::Id::Local);
		menu.horizontalEncoderAction(1);
		LONGS_EQUAL(110, menu.scroll_delay_for_session());
	}
	AudioEngine::audioSampleTimer = 120;
	{
		session::Scope remote(session::Id::Remote);
		menu.beginSession(nullptr);
		menu.horizontalEncoderAction(1);
		LONGS_EQUAL(130, menu.scroll_delay_for_session());
		LONGS_EQUAL(0, automation_views.active().scroll);
	}
	{
		session::Scope local(session::Id::Local);
		menu.horizontalEncoderAction(2);
		LONGS_EQUAL(2, automation_views.active().scroll);
		LONGS_EQUAL(110, menu.scroll_delay_for_session());
	}
	AudioEngine::audioSampleTimer = 131;
	session::Scope remote(session::Id::Remote);
	menu.horizontalEncoderAction(-1);
	LONGS_EQUAL(-1, automation_views.active().scroll);
	LONGS_EQUAL(2, automation_views.for_owner(session::Id::Local).scroll);
}
TEST(PatchStrengthState, missing_and_incompatible_contexts_do_not_access_cables) {
	PatchCableStrength menu;
	session::Scope remote(session::Id::Remote);
	menu.values.active() = 123;
	menu.polarity_for_session() = Polarity::UNIPOLAR;
	for (bool missing : {true, false}) {
		editors.active().currentParamManager = missing ? nullptr : &remote_manager;
		remote_manager.type = ParamManagerType::GLOBAL;
		menu.beginSession(nullptr);
		menu.readCurrentValue();
		menu.setPatchCablePolarity(Polarity::UNIPOLAR);
		LONGS_EQUAL(123, menu.values.active());
		CHECK(menu.polarity_for_session() == Polarity::UNIPOLAR);
	}
	LONGS_EQUAL(0, remote_manager.accesses);
	LONGS_EQUAL(0, local_manager.accesses);
}
TEST(PatchStrengthState, reads_and_polarity_updates_stay_with_active_manager) {
	PatchCableStrength menu;
	local_manager.cables.index = remote_manager.cables.index = 0;
	local_manager.cables.cable.value = 1 << 30;
	remote_manager.cables.cable.value = -(1 << 30);
	{
		session::Scope remote(session::Id::Remote);
		menu.readCurrentValue();
		LONGS_EQUAL(-5000, menu.values.active());
		menu.setPatchCablePolarity(Polarity::UNIPOLAR);
	}
	session::Scope local(session::Id::Local);
	menu.readCurrentValue();
	LONGS_EQUAL(5000, menu.values.active());
	CHECK(local_manager.cables.cable.polarity == Polarity::BIPOLAR);
	CHECK(remote_manager.cables.cable.polarity == Polarity::UNIPOLAR);
	local_manager.cables.index = kNoSelection;
	menu.cable_exists_for_session() = true;
	menu.readCurrentValue();
	LONGS_EQUAL(0, menu.values.active());
	CHECK(!menu.cable_exists_for_session());
	LONGS_EQUAL(-5000, menu.values.for_owner(session::Id::Remote));
}
} // namespace patch_strength_state_test
