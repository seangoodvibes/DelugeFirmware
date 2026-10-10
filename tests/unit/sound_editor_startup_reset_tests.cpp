#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <cstdint>
namespace sound_editor_startup_reset_test {
namespace session = deluge::gui::ui_session;
constexpr uint8_t kNoSelection = 255;
struct SoundEditor {
	void* currentSound = nullptr;
	void* currentModControllable = nullptr;
	void* currentSource = nullptr;
	void* currentParamManager = nullptr;
	void* currentSidechain = nullptr;
	void* currentArpSettings = nullptr;
	void* currentMultiRange = nullptr;
	void* currentSampleControls = nullptr;
	void* currentPriority = nullptr;
	void* currentMIDICable = nullptr;
	int currentSourceIndex = 2, currentMultiRangeIndex = 3;
	void* menuItemNavigationRecord[16]{};
	uint8_t navigationDepth = 3, currentParamShortcutX = 4, currentParamShortcutY = 5;
	bool secondLayerShortcutsToggled = true, shouldGoUpOneLevelOnBegin = true;
	bool setupKitGlobalFXMenu = true, selectedNoteRow = true, haveRenderedPads = true;
	uint32_t timeLastAttemptedAutomatedParamEdit = 99;
	uint8_t shortcutsVersion = 2;
	int blink_resets = 0;
	void resetSourceBlinks() { ++blink_resets; }
	void reset_for_session_startup();
};
#include "sound_editor_startup_reset.inc"
TEST_GROUP(SoundEditorStartupReset){};
TEST(SoundEditorStartupReset, clears_remote_menu_and_model_context_without_modifying_local) {
	session::State<SoundEditor> editors;
	int target = 42;
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		auto& editor = editors.for_owner(owner);
		editor.currentSound = editor.currentModControllable = editor.currentSource = &target;
		editor.currentParamManager = editor.currentSidechain = editor.currentArpSettings = &target;
		editor.currentMultiRange = editor.currentSampleControls = editor.currentPriority = editor.currentMIDICable =
		    &target;
		for (auto& item : editor.menuItemNavigationRecord)
			item = &target;
	}
	{
		session::Scope scope(session::Id::Remote);
		auto& editor = editors.active();
		editor.reset_for_session_startup();
		editor.reset_for_session_startup();
		for (auto ptr :
		     {editor.currentSound, editor.currentModControllable, editor.currentSource, editor.currentParamManager,
		      editor.currentSidechain, editor.currentArpSettings, editor.currentMultiRange,
		      editor.currentSampleControls, editor.currentPriority, editor.currentMIDICable})
			POINTERS_EQUAL(nullptr, ptr);
		for (auto item : editor.menuItemNavigationRecord)
			POINTERS_EQUAL(nullptr, item);
		LONGS_EQUAL(0, editor.navigationDepth);
		LONGS_EQUAL(0, editor.currentSourceIndex);
		LONGS_EQUAL(0, editor.currentMultiRangeIndex);
		LONGS_EQUAL(255, editor.currentParamShortcutX);
		LONGS_EQUAL(255, editor.currentParamShortcutY);
		CHECK_FALSE(editor.secondLayerShortcutsToggled);
		CHECK_FALSE(editor.shouldGoUpOneLevelOnBegin);
		CHECK_FALSE(editor.setupKitGlobalFXMenu);
		CHECK_FALSE(editor.selectedNoteRow);
		CHECK_FALSE(editor.haveRenderedPads);
		LONGS_EQUAL(0, editor.timeLastAttemptedAutomatedParamEdit);
		LONGS_EQUAL(2, editor.blink_resets);
		LONGS_EQUAL(2, editor.shortcutsVersion);
	}
	auto& local = editors.for_owner(session::Id::Local);
	POINTERS_EQUAL(&target, local.currentSound);
	POINTERS_EQUAL(&target, local.currentParamManager);
	for (auto item : local.menuItemNavigationRecord)
		POINTERS_EQUAL(&target, item);
	LONGS_EQUAL(3, local.navigationDepth);
	CHECK_TRUE(local.selectedNoteRow);
	CHECK_TRUE(local.haveRenderedPads);
	LONGS_EQUAL(0, local.blink_resets);
	LONGS_EQUAL(42, target);
}
} // namespace sound_editor_startup_reset_test
