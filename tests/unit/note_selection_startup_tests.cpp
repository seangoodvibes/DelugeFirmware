#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <array>
namespace note_selection_startup_test {
namespace session = deluge::gui::ui_session;
constexpr int kNoSelection = 255;
enum class TimerName { SHORTCUT_BLINK, NOTE_ROW_BLINK };
struct timer_manager {
	session::State<std::array<bool, 2>> active;
	void unsetTimer(TimerName timer) { active.active()[static_cast<int>(timer)] = false; }
} uiTimerManager;
struct InstrumentClipView {
	int lastSelectedNoteXDisplay = 4, lastSelectedNoteYDisplay = 3;
	bool noteRowBlinking = true, noteRowFlashOn = true, sessionMacroSidebarActive = true;
	uint32_t timeSongButtonPressed = 123;
	void reset_selection_display_for_session_startup();
	void resetSelectedNoteBlinking();
	void resetSelectedNoteRowBlinking();
};
#include "note_selection_startup.inc"
TEST_GROUP(NoteSelectionStartup){};
TEST(NoteSelectionStartup, instrument_reset_preserves_local_selection) {
	session::State<InstrumentClipView> instrument_views;
	for (auto owner : {session::Id::Local, session::Id::Remote})
		uiTimerManager.active.for_owner(owner).fill(true);
	session::Scope remote(session::Id::Remote);
	for (auto* views : {&instrument_views}) {
		auto& view = views->active();
		view.reset_selection_display_for_session_startup();
		view.reset_selection_display_for_session_startup();
		LONGS_EQUAL(kNoSelection, view.lastSelectedNoteXDisplay);
		LONGS_EQUAL(kNoSelection, view.lastSelectedNoteYDisplay);
		CHECK(!view.noteRowBlinking);
		CHECK(!view.noteRowFlashOn);
		CHECK(!view.sessionMacroSidebarActive);
		LONGS_EQUAL(0, view.timeSongButtonPressed);
		auto& local = views->for_owner(session::Id::Local);
		LONGS_EQUAL(4, local.lastSelectedNoteXDisplay);
		LONGS_EQUAL(3, local.lastSelectedNoteYDisplay);
		CHECK(local.noteRowBlinking);
		CHECK(local.noteRowFlashOn);
		CHECK(local.sessionMacroSidebarActive);
		LONGS_EQUAL(123, local.timeSongButtonPressed);
	}
	for (bool active : uiTimerManager.active.active())
		CHECK(!active);
	for (bool active : uiTimerManager.active.for_owner(session::Id::Local))
		CHECK(active);
}
} // namespace note_selection_startup_test
