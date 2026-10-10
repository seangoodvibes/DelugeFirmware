#include "CppUTest/TestHarness.h"
#include "gui/ui/ui_session.h"
#include <algorithm>
#include <array>
#include <cstdint>
namespace vu_render_test {
namespace session = ::deluge::gui::ui_session;
constexpr int kDisplayHeight = 8, kDisplayWidth = 16, kSideBarWidth = 2;
struct RGB {
	int value = 0;
};
namespace colours {
constexpr RGB black{0}, green{1}, orange{2}, red{3};
}
namespace PadLEDs {
static session::State<bool> locks;
static bool& rendering_lock_for_session() {
	return locks.active();
}
} // namespace PadLEDs
namespace AudioEngine {
static struct {
	float l = 0, r = 0;
} approxRMSLevel;
} // namespace AudioEngine
constexpr float dBFSForYDisplay[kDisplayHeight] = {-30.8, -26.4, -22.0, -17.6, -13.2, -8.8, -4.4, -0.2};
struct View {
	bool displayVUMeter = true, renderedVUMeter = false;
	bool clip_context = false;
	int mode = 0;
	int cachedMaxYDisplayForVUMeterL = 255, cachedMaxYDisplayForVUMeterR = 255;
	int getModKnobMode() { return mode; }
	bool isClipContext() { return clip_context; }
	int32_t getMaxYDisplayForVUMeter(float);
	void renderVUMeter(int32_t, int32_t, RGB[][kDisplayWidth + kSideBarWidth]);
	bool potentiallyRenderVUMeter(RGB[][kDisplayWidth + kSideBarWidth]);
};
static session::State<View> views;
#include "vu_render.inc"
} // namespace vu_render_test
using namespace vu_render_test;
TEST_GROUP(VURender) {
	RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth];
	void setup() override {
		session::detail::active = session::Id::Local;
		views = {};
		PadLEDs::locks = {};
		AudioEngine::approxRMSLevel = {};
		for (auto& row : image)
			for (auto& pixel : row)
				pixel.value = 9;
	}
	void teardown() override {
		session::detail::active = session::Id::Local;
	}
};
TEST(VURender, level_calculation_covers_silence_each_band_and_clipping) {
	auto& view = views.active();
	LONGS_EQUAL(255, view.getMaxYDisplayForVUMeter(0));
	LONGS_EQUAL(255, view.getMaxYDisplayForVUMeter(8));
	for (int row = 0; row < kDisplayHeight; ++row) {
		// Interior samples avoid floating-point ambiguity at threshold boundaries.
		float level = 16.7f + (dBFSForYDisplay[row] + 0.1f) / 4.0f;
		LONGS_EQUAL(row, view.getMaxYDisplayForVUMeter(level));
	}
	LONGS_EQUAL(7, view.getMaxYDisplayForVUMeter(30));
}
TEST(VURender, stereo_render_preserves_grid_and_draws_sidebar_colors) {
	AudioEngine::approxRMSLevel = {30, 0};
	CHECK(views.active().potentiallyRenderVUMeter(image));
	for (int y = 0; y < kDisplayHeight; ++y) {
		for (int x = 0; x < kDisplayWidth; ++x)
			LONGS_EQUAL(9, image[y][x].value);
		LONGS_EQUAL(y < 5 ? 1 : y < 7 ? 2 : 3, image[y][kDisplayWidth].value);
		LONGS_EQUAL(0, image[y][kDisplayWidth + 1].value);
	}
	CHECK_FALSE(PadLEDs::rendering_lock_for_session());
}
TEST(VURender, changed_level_clears_previous_peak_and_redraws_both_channels) {
	AudioEngine::approxRMSLevel = {30, 30};
	CHECK(views.active().potentiallyRenderVUMeter(image));
	AudioEngine::approxRMSLevel = {0, 10};
	CHECK(views.active().potentiallyRenderVUMeter(image));
	for (int y = 0; y < kDisplayHeight; ++y) {
		LONGS_EQUAL(0, image[y][kDisplayWidth].value);
		LONGS_EQUAL(y == 0 ? 1 : 0, image[y][kDisplayWidth + 1].value);
	}
}
TEST(VURender, cached_render_is_panel_local_and_reenabled_meter_redraws) {
	AudioEngine::approxRMSLevel = {30, 30};
	CHECK(views.active().potentiallyRenderVUMeter(image));
	image[0][kDisplayWidth].value = 9;
	CHECK(views.active().potentiallyRenderVUMeter(image));
	LONGS_EQUAL(9, image[0][kDisplayWidth].value);
	{
		session::Scope scope(session::Id::Remote);
		CHECK(views.active().potentiallyRenderVUMeter(image));
		LONGS_EQUAL(1, image[0][kDisplayWidth].value);
		CHECK_FALSE(PadLEDs::rendering_lock_for_session());
	}
	views.active().displayVUMeter = false;
	CHECK_FALSE(views.active().potentiallyRenderVUMeter(image));
	image[0][kDisplayWidth].value = 9;
	views.active().displayVUMeter = true;
	CHECK(views.active().potentiallyRenderVUMeter(image));
	LONGS_EQUAL(1, image[0][kDisplayWidth].value);
}
