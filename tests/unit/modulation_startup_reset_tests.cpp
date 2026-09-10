#include "CppUTest/TestHarness.h"
#include "gui/ui_timer_state.h"
#include "model/model_stack.h"
#include <cstring>
namespace modulation_startup_reset_test {
namespace session = deluge::gui::ui_session;
struct View {
	ModelStackWithThreeMainThings activeModControllableModelStack;
	uint8_t dummy[MODEL_STACK_MAX_SIZE - sizeof(ModelStackWithThreeMainThings)];
	uint32_t modLength = 24, modPos = 48;
	int32_t modNoteRowId = 7;
	bool pendingParamAutomationUpdatesModLevels = true;
	bool hasPendingModEncoderValuePopup = true;
	void reset_modulation_for_session_startup();
};
struct Timers {
	UITimerState state;
	void unsetTimer(TimerName timer) { state.unset(timer, 0); }
};
static Timers uiTimerManager;
#include "modulation_startup_reset.inc"
TEST_GROUP(ModulationStartupReset){};
TEST(ModulationStartupReset, clears_remote_model_storage_region_and_pending_work_preserving_local) {
	session::State<View> views;
	uiTimerManager = {};
	for (auto owner : {session::Id::Local, session::Id::Remote}) {
		session::Scope scope(owner);
		auto& view = views.active();
		// Poison retained storage; cleanup must not dereference any old target.
		memset(&view.activeModControllableModelStack, 0x5a, sizeof(view.activeModControllableModelStack));
		memset(view.dummy, 0x5a, sizeof(view.dummy));
		uiTimerManager.state.set(TimerName::MOD_ENCODER_POPUP_FLUSH, 0, 50);
	}
	{
		session::Scope scope(session::Id::Remote);
		auto& view = views.active();
		view.reset_modulation_for_session_startup();
		view.reset_modulation_for_session_startup();
		auto* bytes = reinterpret_cast<const unsigned char*>(&view.activeModControllableModelStack);
		for (size_t i = 0; i < sizeof(view.activeModControllableModelStack); ++i)
			LONGS_EQUAL(0, bytes[i]);
		for (auto byte : view.dummy)
			LONGS_EQUAL(0, byte);
		LONGS_EQUAL(0, view.modLength);
		UNSIGNED_LONGS_EQUAL(0xFFFFFFFF, view.modPos);
		LONGS_EQUAL(0, view.modNoteRowId);
		CHECK_FALSE(view.pendingParamAutomationUpdatesModLevels);
		CHECK_FALSE(view.hasPendingModEncoderValuePopup);
		CHECK_FALSE(uiTimerManager.state.get(TimerName::MOD_ENCODER_POPUP_FLUSH).active);
	}
	{
		session::Scope scope(session::Id::Local);
		auto& view = views.active();
		auto* bytes = reinterpret_cast<const unsigned char*>(&view.activeModControllableModelStack);
		for (size_t i = 0; i < sizeof(view.activeModControllableModelStack); ++i)
			LONGS_EQUAL(0x5a, bytes[i]);
		for (auto byte : view.dummy)
			LONGS_EQUAL(0x5a, byte);
		LONGS_EQUAL(24, view.modLength);
		LONGS_EQUAL(48, view.modPos);
		LONGS_EQUAL(7, view.modNoteRowId);
		CHECK_TRUE(view.pendingParamAutomationUpdatesModLevels);
		CHECK_TRUE(view.hasPendingModEncoderValuePopup);
		CHECK_TRUE(uiTimerManager.state.get(TimerName::MOD_ENCODER_POPUP_FLUSH).active);
	}
}
} // namespace modulation_startup_reset_test
