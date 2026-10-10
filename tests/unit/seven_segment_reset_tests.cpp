#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include "hid/display/seven_segment_frame.h"

namespace seven_segment_reset_test {
namespace session = deluge::gui::ui_session;
using deluge::hid::display::SevenSegmentFrame;
enum class TimerName { DISPLAY };
enum class PopupType { NONE, GENERAL };
static int destroyed, freed, resumed, renders;
struct NumericLayer {
	NumericLayer* next = nullptr;
	virtual ~NumericLayer() { ++destroyed; }
	void isNowOnTop() { ++resumed; }
};
struct RetainedLayer : NumericLayer {
	static inline int destroyed_layers;
	~RetainedLayer() override { ++destroyed_layers; }
};
void delugeDealloc(void* layer) {
	++freed;
	::operator delete(layer);
}
struct TimerManager {
	session::State<bool> active;
	void unsetTimer(TimerName) { active.active() = false; }
};
static TimerManager uiTimerManager;
struct Panel {
	NumericLayer* topLayer = nullptr;
	bool popupActive = false;
	PopupType popupType = PopupType::NONE;
	int nextTransitionDirection = 0;
	SevenSegmentFrame frame;
};
struct SevenSegment {
	session::State<Panel> panels;
	Panel& panel_state() { return panels.active(); }
	void render() { ++renders; }
	void deleteAllLayers();
	void reset_layers_for_session();
};
#include "seven_segment_reset.inc"
TEST_GROUP(SevenSegmentReset){void setup()
                                  override{destroyed = freed = resumed = renders = RetainedLayer::destroyed_layers = 0;
uiTimerManager.active = {};
} // namespace seven_segment_reset_test
}
;
TEST(SevenSegmentReset, clears_remote_layers_and_popup_without_resuming_them_or_touching_local) {
	SevenSegment display;
	NumericLayer local_layer;
	auto& local = display.panels.for_owner(session::Id::Local);
	local.topLayer = &local_layer;
	local.popupActive = true;
	local.popupType = PopupType::GENERAL;
	local.nextTransitionDirection = -1;
	local.frame.publish({1, 2, 3, 4});
	uiTimerManager.active.for_owner(session::Id::Local) = true;
	{
		session::Scope scope(session::Id::Remote);
		auto& remote = display.panel_state();
		remote.topLayer = new RetainedLayer;
		remote.topLayer->next = new RetainedLayer;
		remote.popupActive = true;
		remote.popupType = PopupType::GENERAL;
		remote.nextTransitionDirection = 1;
		remote.frame.publish({5, 6, 7, 8});
		uiTimerManager.active.active() = true;
		display.reset_layers_for_session();
		POINTERS_EQUAL(nullptr, remote.topLayer);
		CHECK_FALSE(remote.popupActive);
		CHECK(remote.popupType == PopupType::NONE);
		LONGS_EQUAL(0, remote.nextTransitionDirection);
		CHECK_FALSE(uiTimerManager.active.active());
		for (auto segment : remote.frame.segments)
			LONGS_EQUAL(0, segment);
		LONGS_EQUAL(0, remote.frame.revision);
		display.reset_layers_for_session(); // Empty reconnect cleanup is idempotent.
	}
	LONGS_EQUAL(2, destroyed);
	LONGS_EQUAL(2, RetainedLayer::destroyed_layers);
	LONGS_EQUAL(2, freed);
	LONGS_EQUAL(0, resumed);
	LONGS_EQUAL(0, renders);
	POINTERS_EQUAL(&local_layer, local.topLayer);
	CHECK_TRUE(local.popupActive);
	CHECK(local.popupType == PopupType::GENERAL);
	LONGS_EQUAL(-1, local.nextTransitionDirection);
	CHECK_TRUE(uiTimerManager.active.for_owner(session::Id::Local));
	for (int i = 0; i < 4; ++i)
		LONGS_EQUAL(i + 1, local.frame.segments[i]);
	LONGS_EQUAL(1, local.frame.revision);
}
} // namespace seven_segment_reset_test
