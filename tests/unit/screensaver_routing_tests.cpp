#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "gui/ui_timer_state.h"
namespace session = deluge::gui::ui_session;
enum class ScreensaverMode { OFF, BLANK, STARSCAPE, DELUGE };
namespace FlashStorage {
ScreensaverMode screensaverMode = ScreensaverMode::BLANK;
int screensaverTimeoutMinutes = 5;
} // namespace FlashStorage
struct display_fixture {
	bool oled = true;
	bool haveOLED() { return oled; }
} panel;
auto* display = &panel;
struct timer_fixture {
	UITimerState state;
	void setTimer(TimerName name, int delay) { state.set(name, 0, delay); }
	void unsetTimer(TimerName name) { state.get(name).active = false; }
	int deadline(session::Id owner = session::current()) {
		const auto& timer = state.bank(owner).timers[static_cast<size_t>(TimerName::SCREENSAVER)];
		return timer.active ? static_cast<int>(timer.triggerTime) : -1;
	}
	void seed_remote() {
		state.bank(session::Id::Remote).timers[static_cast<size_t>(TimerName::SCREENSAVER)] = {true, 123};
	}
} uiTimerManager;
struct {
	bool processStarted = false;
} stemExport;
namespace deluge::hid::display {
constexpr int kMillisecondsPerMinute = 60000, kFrameIntervalMS = 50, kInhibitRecheckMS = 1000;
bool isAnimated(ScreensaverMode mode) {
	return mode == ScreensaverMode::STARSCAPE || mode == ScreensaverMode::DELUGE;
}
struct OLED {
	static inline session::State<int> dirty;
	static inline session::State<bool> popup;
	static bool isPermanentPopupPresent() { return popup.active(); }
	static bool isWorkingAnimationPresent() { return false; }
	static void markChanged() { ++dirty.active(); }
};
struct animation_fixture {
	int calls = 0;
	void scatter() { ++calls; }
	void advance() { ++calls; }
};
struct Screensaver {
	static inline bool active_ = false;
	static inline int renders = 0;
	static inline animation_fixture starfield_, rainfall_;
	static void arm();
	static void noteActivity();
	static void timerEvent();
	static void settingsChanged();
	static void render() { ++renders; }
};
#include "screensaver_events.inc"
} // namespace deluge::hid::display
using deluge::hid::display::OLED;
using deluge::hid::display::Screensaver;
TEST_GROUP(ScreensaverRouting){void setup() override{session::detail::active = session::Id::Local;
Screensaver::active_ = false;
Screensaver::renders = 0;
Screensaver::starfield_ = {};
Screensaver::rainfall_ = {};
uiTimerManager = {};
OLED::dirty = {};
OLED::popup = {};
panel.oled = true;
FlashStorage::screensaverMode = ScreensaverMode::BLANK;
}
void teardown() override {
	session::detail::active = session::Id::Local;
}
}
;
TEST(ScreensaverRouting, remote_activity_preserves_local_sleep_and_both_deadlines) {
	Screensaver::timerEvent();
	const int deadline = uiTimerManager.deadline();
	const int dirty = OLED::dirty.active();
	session::Scope owner(session::Id::Remote);
	uiTimerManager.seed_remote();
	Screensaver::noteActivity();
	CHECK(Screensaver::active_);
	LONGS_EQUAL(deadline, uiTimerManager.deadline(session::Id::Local));
	LONGS_EQUAL(123, uiTimerManager.deadline());
	LONGS_EQUAL(dirty, OLED::dirty.for_owner(session::Id::Local));
	LONGS_EQUAL(0, OLED::dirty.active());
}
TEST(ScreensaverRouting, remote_timer_cannot_activate_advance_or_inhibit_local_animation) {
	FlashStorage::screensaverMode = ScreensaverMode::STARSCAPE;
	{
		session::Scope owner(session::Id::Remote);
		Screensaver::timerEvent();
		CHECK_FALSE(Screensaver::active_);
	}
	Screensaver::timerEvent();
	LONGS_EQUAL(1, Screensaver::starfield_.calls);
	{
		session::Scope owner(session::Id::Remote);
		Screensaver::timerEvent();
		OLED::popup.active() = true;
		Screensaver::timerEvent();
		CHECK(Screensaver::active_);
		LONGS_EQUAL(1, Screensaver::starfield_.calls);
		LONGS_EQUAL(1, Screensaver::renders);
		LONGS_EQUAL(-1, uiTimerManager.deadline());
		LONGS_EQUAL(0, OLED::dirty.active());
	}
}
TEST(ScreensaverRouting, settings_from_either_panel_update_local_timer_and_restore_owner) {
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		for (auto mode : {ScreensaverMode::OFF, ScreensaverMode::BLANK}) {
			Screensaver::active_ = true;
			OLED::dirty = {};
			uiTimerManager.seed_remote();
			FlashStorage::screensaverMode = mode;
			session::Scope scope(owner);
			Screensaver::settingsChanged();
			CHECK(session::current() == owner);
			CHECK_FALSE(Screensaver::active_);
			LONGS_EQUAL(1, OLED::dirty.for_owner(session::Id::Local));
			LONGS_EQUAL(0, OLED::dirty.for_owner(session::Id::Remote));
			LONGS_EQUAL(mode == ScreensaverMode::OFF ? -1 : 300000, uiTimerManager.deadline(session::Id::Local));
			LONGS_EQUAL(123, uiTimerManager.deadline(session::Id::Remote));
		}
	}
}
TEST(ScreensaverRouting, local_activity_still_wakes_and_rearms) {
	Screensaver::timerEvent();
	Screensaver::noteActivity();
	CHECK_FALSE(Screensaver::active_);
	LONGS_EQUAL(300000, uiTimerManager.deadline());
	LONGS_EQUAL(2, OLED::dirty.active());
}
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
