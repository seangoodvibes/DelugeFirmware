#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <array>
namespace recorder_menu_routing_test {
namespace ui_session = deluge::gui::ui_session;
struct MenuItem {};
struct HorizontalMenu : MenuItem {
	MenuItem* focused = nullptr;
	void focusChild(MenuItem* item) { focused = item; }
};
struct editor {
	bool shouldGoUpOneLevelOnBegin = false;
	int navigationDepth = 2, source = -1, ascents = 0;
	std::array<MenuItem*, 4> menuItemNavigationRecord{};
	void setCurrentSource(int value) { source = value; }
	void goUpOneLevel() { ++ascents; }
};
struct recorder {
	int processes = 0;
	void process() { ++processes; }
};
ui_session::State<editor> editors;
ui_session::State<recorder> recorders;
editor& sound_editor_for_session() {
	return editors.active();
}
recorder& audio_recorder_for_session() {
	return recorders.active();
}
bool open_success = true, editor_current = true;
void* opened_recorder = nullptr;
bool openUI(recorder* value) {
	opened_recorder = value;
	return open_success;
}
void* getCurrentUI() {
	return editor_current ? &sound_editor_for_session() : nullptr;
}
enum class TimerName { SHORTCUT_BLINK };
struct timer_manager {
	ui_session::State<int> cancellations;
	void unsetTimer(TimerName) { ++cancellations.active(); }
} uiTimerManager;
struct AudioRecorder {
	ui_session::State<HorizontalMenu*> destination_parents;
	ui_session::State<MenuItem*> destinations;
	uint8_t source_id_ = 1;
	HorizontalMenu*& destination_parent_for_session();
	MenuItem*& destination_for_session();
	void beginSession(MenuItem*);
};
#include "recorder_menu_routing.inc"
TEST_GROUP(RecorderMenuRouting){void setup() override{editors = {};
recorders = {};
uiTimerManager.cancellations = {};
open_success = editor_current = true;
opened_recorder = nullptr;
} // namespace recorder_menu_routing_test
}
;
TEST(RecorderMenuRouting, entry_consumes_only_active_sessions_destination) {
	AudioRecorder menu;
	HorizontalMenu local_parent, remote_parent;
	MenuItem local_child, remote_child;
	{
		ui_session::Scope local(ui_session::Id::Local);
		menu.destination_parent_for_session() = &local_parent;
		menu.destination_for_session() = &local_child;
	}
	{
		ui_session::Scope remote(ui_session::Id::Remote);
		CHECK(menu.destination_for_session() == nullptr);
		menu.destination_parent_for_session() = &remote_parent;
		menu.destination_for_session() = &remote_child;
		menu.beginSession(nullptr);
		CHECK(remote_parent.focused == &remote_child);
		CHECK(sound_editor_for_session().menuItemNavigationRecord[0] == &remote_parent);
		CHECK(!sound_editor_for_session().shouldGoUpOneLevelOnBegin);
		CHECK(menu.destination_for_session() == nullptr);
		CHECK(menu.destination_parent_for_session() == nullptr);
		CHECK(opened_recorder == &recorders.active());
		LONGS_EQUAL(1, recorders.active().processes);
		LONGS_EQUAL(1, sound_editor_for_session().source);
	}
	ui_session::Scope local(ui_session::Id::Local);
	CHECK(menu.destination_for_session() == &local_child);
	CHECK(local_parent.focused == nullptr);
	LONGS_EQUAL(2, sound_editor_for_session().navigationDepth);
	menu.beginSession(nullptr);
	CHECK(local_parent.focused == &local_child);
	CHECK(sound_editor_for_session().menuItemNavigationRecord[0] == &local_parent);
	LONGS_EQUAL(1, recorders.active().processes);
}
TEST(RecorderMenuRouting, failed_open_unwinds_and_cancels_only_active_session) {
	AudioRecorder menu;
	ui_session::Scope remote(ui_session::Id::Remote);
	open_success = false;
	menu.beginSession(nullptr);
	CHECK(sound_editor_for_session().shouldGoUpOneLevelOnBegin);
	LONGS_EQUAL(1, sound_editor_for_session().ascents);
	LONGS_EQUAL(1, uiTimerManager.cancellations.active());
	LONGS_EQUAL(0, recorders.active().processes);
	LONGS_EQUAL(0, editors.for_owner(ui_session::Id::Local).ascents);
	LONGS_EQUAL(0, uiTimerManager.cancellations.for_owner(ui_session::Id::Local));
	editor_current = false;
	menu.beginSession(nullptr);
	LONGS_EQUAL(1, sound_editor_for_session().ascents);
	LONGS_EQUAL(2, uiTimerManager.cancellations.active());
}
} // namespace recorder_menu_routing_test
